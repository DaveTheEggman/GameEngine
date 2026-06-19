// Full GPU-texture asset pipeline: author a TextureAsset (image file + sampler
// intent) -> cook with TextureAssetBuilder into an output content DB -> load the
// cooked TextureResource and build a live GPU Texture via the device-backed
// factory (Null RHI backend). Also exercises the TextureImporter authoring helper.
#include <doctest/doctest.h>
#include "Core/Prelude.h"
#include "Core/Reflection/Reflect.h"
import raptor.core;
import raptor.vfs;
import raptor.content;
import raptor.resource;
import raptor.rhi;
import raptor.rhi.null;
import raptor.editor;
import raptor.image;
import raptor.image.io;
import raptor.texture;
import raptor.texture.resource;
import raptor.texture.editor;

using namespace raptor::core;
using namespace raptor::vfs;
using namespace raptor::resource;
using namespace raptor::texture;
namespace img = raptor::image;
namespace iio = raptor::image::io;
namespace rhi = raptor::rhi;

namespace
{
    void RemoveTree()
    {
        FileDelete(u8"raptor_texpipe_src.png");
        FileDelete(u8"raptor_texpipe_out_db/diffuse.rasset");
        FileDelete(u8"raptor_texpipe_out_db/diffuse.data.bin");
        RemoveDirectory(u8"raptor_texpipe_out_db");
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
        REQUIRE(iio::SaveImage(src, u8"raptor_texpipe_src.png", iio::ImageFileFormat::PNG).IsOk());
    }

    NativeFileSystem outMount(u8"raptor_texpipe_out_db");
    Guid id;

    // --- cook (tooling): TextureAsset -> TextureResource in the output DB ---
    {
        raptor::content::ContentDatabase outDb(outMount);
        auto* inst = outDb.RootGroup()->CreateInstance(u8"diffuse", TextureResource::StaticType());
        id = inst->Id();

        TextureAsset asset;
        asset.fileName = u8"raptor_texpipe_src.png";
        asset.SetupForUI();                 // clamp, no mips
        asset.colorSpace = img::ImageColorSpace::Srgb;

        TextureAssetBuilder builder;
        REQUIRE(builder.AssetType() == &TextureAsset::StaticType());
        raptor::editor::AssetBuildContext ctx{ u8"", inst };
        REQUIRE(builder.Build(asset, ctx).IsOk());
    }

    // --- runtime load: cooked TextureResource -> live GPU Texture (model A) ---
    rhi::null::NullDevice device;
    raptor::content::ContentDatabase outDb(outMount);
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
    NativeFileSystem outMount(u8"raptor_texpipe_out_db");
    raptor::content::ContentDatabase outDb(outMount);
    auto* inst = outDb.RootGroup()->CreateInstance(u8"diffuse", TextureResource::StaticType());

    TextureAsset asset;
    asset.fileName = u8"does_not_exist_xyz.png";
    TextureAssetBuilder builder;
    raptor::editor::AssetBuildContext ctx{ u8"", inst };
    CHECK_FALSE(builder.Build(asset, ctx).IsOk());

    RemoveTree();
}
