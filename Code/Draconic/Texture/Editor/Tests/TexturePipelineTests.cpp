// Full GPU-texture asset pipeline: author a TextureAsset (image file + sampler
// intent) -> cook with TextureAssetBuilder into an output content DB -> load the
// cooked TextureResource and build a live GPU Texture via the device-backed
// factory (Null RHI backend). Also exercises the TextureImporter authoring helper.
#include <doctest/doctest.h>
#include "Core/Prelude.h"
#include <initializer_list>
#include "Core/Reflection/Reflect.h"
import draconic.core;
import draconic.vfs;
import draconic.content;
import draconic.resource;
import draconic.rhi;
import draconic.rhi.null;
import draconic.editor;
import draconic.editor.core;
import draconic.image;
import draconic.image.io;
import draconic.texture;
import draconic.texture.resource;
import draconic.texture.editor;

using namespace draconic::core;
using namespace draconic::vfs;
using namespace draconic::resource;
using namespace draconic::texture;
namespace image = draconic::image;
namespace rhi = draconic::rhi;

namespace
{
    void RemoveTree()
    {
        FileDelete(u8"draconic_texpipe_src.png");
        FileDelete(u8"draconic_texpipe_out_db/diffuse.rasset");
        FileDelete(u8"draconic_texpipe_out_db/diffuse.data.bin");
        RemoveDirectory(u8"draconic_texpipe_out_db");
    }
}

TEST_CASE("texture.pipeline: TextureAsset -> cook -> GPU Texture")
{
    RegisterTextureResource();
    RegisterTextureAsset();
    RemoveTree();

    // A known 2x2 RGBA source image on disk.
    {
        image::Image src(2, 2, image::PixelFormat::RGBA8);
        Span<u8> px = src.PixelDataMut();
        for (usize i = 0; i < px.Size(); ++i) { px.Data()[i] = static_cast<u8>(i * 5); }
        REQUIRE(image::io::SaveImage(src, u8"draconic_texpipe_src.png", image::io::ImageFileFormat::PNG).IsOk());
    }

    NativeFileSystem outMount(u8"draconic_texpipe_out_db");
    Guid id;

    // --- cook (tooling): TextureAsset -> TextureResource in the output DB ---
    {
        draconic::content::ContentDatabase outDb(outMount, draconic::core::BinarySerializerFactory(), u8".rasset");
        auto* inst = outDb.RootGroup()->CreateInstance(u8"diffuse", TextureResource::StaticType());
        id = inst->Id();

        TextureAsset asset;
        asset.fileName = u8"draconic_texpipe_src.png";
        asset.SetupForUI();                 // clamp, no mips
        asset.colorSpace = image::ImageColorSpace::Srgb;

        TextureAssetBuilder builder;
        REQUIRE(builder.AssetType() == &TextureAsset::StaticType());
        draconic::vfs::NativeFileSystem srcMount(u8".");
        draconic::editor::AssetBuildContext ctx;
        ctx.sources = &srcMount;
        ctx.output = inst;
        REQUIRE(builder.Build(asset, ctx).IsOk());
    }

    // --- runtime load: cooked TextureResource -> live GPU Texture (model A) ---
    rhi::null::NullDevice device{DefaultAllocator()};
    draconic::content::ContentDatabase outDb(outMount, draconic::core::BinarySerializerFactory(), u8".rasset");
    TextureFactory factory(device);
    ResourceManager manager(outDb);
    manager.AddFactory(&factory);

    Proxy<Texture> tex = manager.Bind<Texture>(id);
    REQUIRE(tex);
    CHECK(tex->Width() == 2u);
    CHECK(tex->Height() == 2u);
    CHECK(tex->Format() == rhi::TextureFormat::RGBA8UnormSrgb); // sRGB resolved from color space
    CHECK(tex->GpuTexture() != nullptr);
    CHECK(tex->Sampler() != nullptr);

    RemoveTree();
}

TEST_CASE("texture.importer: produces a TextureAsset with the right preset")
{
    TextureAsset a;
    TextureImporter::Import2D(u8"art/brick.png", image::ImageColorSpace::Srgb, a);
    CHECK(a.fileName == String(u8"art/brick.png"));
    CHECK(a.colorSpace == image::ImageColorSpace::Srgb);
    CHECK(a.generateMipmaps);                       // 3D preset
    CHECK(a.minFilter == TextureFilter::MipmapLinear);

    TextureAsset sky;
    TextureImporter::ImportEquirectangular(u8"sky/dusk.hdr", sky);
    CHECK(sky.colorSpace == image::ImageColorSpace::Linear);
    CHECK(sky.wrapU == TextureWrap::ClampToEdge);
    CHECK_FALSE(sky.generateMipmaps);
}

TEST_CASE("texture.pipeline: builder fails on a missing source file")
{
    RegisterTextureResource();
    RegisterTextureAsset();
    RemoveTree();
    NativeFileSystem outMount(u8"draconic_texpipe_out_db");
    draconic::content::ContentDatabase outDb(outMount, draconic::core::BinarySerializerFactory(), u8".rasset");
    auto* inst = outDb.RootGroup()->CreateInstance(u8"diffuse", TextureResource::StaticType());

    TextureAsset asset;
    asset.fileName = u8"does_not_exist_xyz.png";
    TextureAssetBuilder builder;
    draconic::vfs::NativeFileSystem srcMount(u8".");
        draconic::editor::AssetBuildContext ctx;
        ctx.sources = &srcMount;
        ctx.output = inst;
    CHECK_FALSE(builder.Build(asset, ctx).IsOk());

    RemoveTree();
}

TEST_CASE("texture-import: drag-dropped file becomes a Sources copy + TextureAsset instance")
{
    RegisterTextureAsset();

    const StringView dir = u8"draconic_tex_import_project";
    auto cleanTree = [&]() {
        FileDelete(PathJoin(dir, u8"Project.xml"));
        FileDelete(PathJoin(dir, u8"Sources/brick.png"));
        FileDelete(PathJoin(dir, u8"Content/brick.xasset"));
        for (StringView sub : { u8"Content", u8"Sources", u8"Cooked", u8"Editor", u8".cache" })
        {
            RemoveDirectory(PathJoin(dir, sub));
        }
        RemoveDirectory(dir);
    };
    cleanTree();
    REQUIRE(draconic::editor::EditorProject::Create(dir, u8"P").IsOk());
    UniquePtr<draconic::editor::EditorProject> project = draconic::editor::EditorProject::Open(dir);
    REQUIRE(static_cast<bool>(project));

    // A loose "PNG" (the importer copies bytes + creates the asset; decoding happens at cook).
    const byte fakePng[6] = { byte{'P'}, byte{'N'}, byte{'G'}, byte{1}, byte{2}, byte{3} };
    REQUIRE(WriteFile(u8"brick.png", Span<const byte>(fakePng, 6)).IsOk());

    TextureFileImporter importer;
    CHECK(importer.Accepts(u8"png"));
    CHECK(importer.Accepts(u8"jpeg"));
    CHECK_FALSE(importer.Accepts(u8"gltf"));

    Result<draconic::content::Instance*> imported =
        importer.Import(u8"brick.png", *project, *project->SourceDb().RootGroup());
    REQUIRE(imported.HasValue());
    draconic::content::Instance* instance = imported.Value();
    REQUIRE(instance != nullptr);
    CHECK(instance->Name() == StringView(u8"brick"));
    CHECK(instance->TypeName() == StringView(u8"TextureAsset"));

    // The source landed in Sources/ and the asset references it by mount-relative name.
    CHECK(FileExists(PathJoin(dir, u8"Sources/brick.png").AsView()));
    RefPtr<ISerializable> object = instance->ReadObject();
    auto* asset = Cast<TextureAsset>(object.Get());
    REQUIRE(asset != nullptr);
    CHECK(asset->fileName == StringView(u8"brick.png"));
    CHECK(asset->generateMipmaps);   // the 3D preset

    FileDelete(u8"brick.png");
    cleanTree();
}
