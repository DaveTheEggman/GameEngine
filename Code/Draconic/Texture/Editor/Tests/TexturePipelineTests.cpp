// Full GPU-texture asset pipeline: author a TextureAsset (image file + sampler
// intent) -> cook with TextureAssetBuilder into an output content DB -> load the
// cooked TextureResource and build a live GPU Texture via the device-backed
// factory (Null RHI backend). Also exercises the TextureImporter authoring helper.
#include <doctest/doctest.h>
#include "Core/Prelude.h"
#include "Core/Reflection/Reflect.h"
import draconic.core;
import draconic.vfs;
import draconic.content;
import draconic.resource;
import draconic.rhi;
import draconic.rhi.null;
import draconic.editor;
import draconic.image;
import draconic.image.io;
import draconic.texture;
import draconic.texture.resource;
import draconic.texture.editor;

using namespace draconic::core;
using namespace draconic::vfs;
using namespace draconic::resource;
using namespace draconic::texture;
namespace img = draconic::image;
namespace iio = draconic::image::io;
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
        img::Image src(2, 2, img::PixelFormat::RGBA8);
        Span<u8> px = src.PixelDataMut();
        for (usize i = 0; i < px.Size(); ++i) { px.Data()[i] = static_cast<u8>(i * 5); }
        REQUIRE(iio::SaveImage(src, u8"draconic_texpipe_src.png", iio::ImageFileFormat::PNG).IsOk());
    }

    NativeFileSystem outMount(u8"draconic_texpipe_out_db");
    Guid id;

    // --- cook (tooling): TextureAsset -> TextureResource in the output DB ---
    {
        draconic::content::ContentDatabase outDb(outMount);
        auto* inst = outDb.RootGroup()->CreateInstance(u8"diffuse", TextureResource::StaticType());
        id = inst->Id();

        TextureAsset asset;
        asset.fileName = u8"draconic_texpipe_src.png";
        asset.SetupForUI();                 // clamp, no mips
        asset.colorSpace = img::ImageColorSpace::Srgb;

        TextureAssetBuilder builder;
        REQUIRE(builder.AssetType() == &TextureAsset::StaticType());
        draconic::editor::AssetBuildContext ctx{ u8"", inst };
        REQUIRE(builder.Build(asset, ctx).IsOk());
    }

    // --- runtime load: cooked TextureResource -> live GPU Texture (model A) ---
    rhi::null::NullDevice device;
    draconic::content::ContentDatabase outDb(outMount);
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
    TextureImporter::Import2D(u8"art/brick.png", img::ImageColorSpace::Srgb, a);
    CHECK(a.fileName == String(u8"art/brick.png"));
    CHECK(a.colorSpace == img::ImageColorSpace::Srgb);
    CHECK(a.generateMipmaps);                       // 3D preset
    CHECK(a.minFilter == TextureFilter::MipmapLinear);

    TextureAsset sky;
    TextureImporter::ImportEquirectangular(u8"sky/dusk.hdr", sky);
    CHECK(sky.colorSpace == img::ImageColorSpace::Linear);
    CHECK(sky.wrapU == TextureWrap::ClampToEdge);
    CHECK_FALSE(sky.generateMipmaps);
}

TEST_CASE("texture.pipeline: builder fails on a missing source file")
{
    RegisterTextureResource();
    RegisterTextureAsset();
    RemoveTree();
    NativeFileSystem outMount(u8"draconic_texpipe_out_db");
    draconic::content::ContentDatabase outDb(outMount);
    auto* inst = outDb.RootGroup()->CreateInstance(u8"diffuse", TextureResource::StaticType());

    TextureAsset asset;
    asset.fileName = u8"does_not_exist_xyz.png";
    TextureAssetBuilder builder;
    draconic::editor::AssetBuildContext ctx{ u8"", inst };
    CHECK_FALSE(builder.Build(asset, ctx).IsOk());

    RemoveTree();
}
