// Full CPU-image asset pipeline: author an ImageAsset (image file) -> cook with
// ImageAssetBuilder into an output content DB -> load the cooked ImageResource
// through the ResourceManager (device-free, model B). PNG round-trips RGBA8.
#include <doctest/doctest.h>
#include "Core/Prelude.h"
#include "Core/Reflection/Reflect.h"
import raptor.core;
import raptor.vfs;
import raptor.content;
import raptor.resource;
import raptor.editor;
import raptor.image;
import raptor.image.io;
import raptor.image.resource;
import raptor.image.editor;

using namespace raptor::core;
using namespace raptor::vfs;
using namespace raptor::resource;
using namespace raptor::image;
namespace iio = raptor::image::io;

namespace
{
    void RemoveTree()
    {
        FileDelete(u8"raptor_imgpipe_src.png");
        FileDelete(u8"raptor_imgpipe_out_db/icon.rasset");
        FileDelete(u8"raptor_imgpipe_out_db/icon.pixels.bin");
        RemoveDirectory(u8"raptor_imgpipe_out_db");
    }
}

TEST_CASE("image.pipeline: ImageAsset -> cook -> ImageResource round-trips")
{
    RegisterImageResource();
    RegisterImageAsset();
    RemoveTree();

    // A known 2x2 RGBA source image on disk (the art the asset references).
    {
        Image src(2, 2, PixelFormat::RGBA8);
        Span<u8> px = src.PixelDataMut();
        for (usize i = 0; i < px.Size(); ++i) { px.Data()[i] = static_cast<u8>(i * 7); }
        REQUIRE(iio::SaveImage(src, u8"raptor_imgpipe_src.png", iio::ImageFileFormat::PNG).IsOk());
    }

    NativeFileSystem outMount(u8"raptor_imgpipe_out_db");

    // --- cook (tooling): ImageAsset -> ImageResource in the output DB ---
    Guid id;
    {
        raptor::content::ContentDatabase outDb(outMount);
        auto* inst = outDb.RootGroup()->CreateInstance(u8"icon", ImageResource::StaticType());
        id = inst->Id();

        ImageAsset asset;
        asset.fileName = u8"raptor_imgpipe_src.png";
        asset.colorSpace = ImageColorSpace::Srgb;

        ImageAssetBuilder builder;
        raptor::editor::AssetBuildContext ctx{ u8"", inst };
        REQUIRE(builder.Build(asset, ctx).IsOk());
    }

    // --- runtime load (device-free): cooked ImageResource via the manager ---
    raptor::content::ContentDatabase outDb(outMount);
    ImageFactory factory;
    ResourceManager manager(outDb);
    manager.AddFactory(&factory);

    Proxy<ImageResource> img = manager.Bind<ImageResource>(id);
    REQUIRE(img);
    CHECK(img->width == 2u);
    CHECK(img->height == 2u);
    CHECK(img->format == PixelFormat::RGBA8);
    CHECK(img->colorSpace == ImageColorSpace::Srgb);

    REQUIRE(img->Pixels().Size() == 2u * 2u * 4u);
    bool match = true;
    for (usize i = 0; i < img->Pixels().Size(); ++i) { if (img->Pixels().Data()[i] != static_cast<u8>(i * 7)) { match = false; break; } }
    CHECK(match);

    // The IImageData view points at the resource's pixels.
    ImageDataRef view = img->View();
    CHECK(view.Width() == 2u);
    CHECK(view.PixelData().Data() == img->Pixels().Data());

    RemoveTree();
}

TEST_CASE("image.pipeline: builder fails on a missing source file")
{
    RegisterImageResource();
    RegisterImageAsset();
    RemoveTree();
    NativeFileSystem outMount(u8"raptor_imgpipe_out_db");
    raptor::content::ContentDatabase outDb(outMount);
    auto* inst = outDb.RootGroup()->CreateInstance(u8"icon", ImageResource::StaticType());

    ImageAsset asset;
    asset.fileName = u8"does_not_exist_xyz.png";
    ImageAssetBuilder builder;
    raptor::editor::AssetBuildContext ctx{ u8"", inst };
    CHECK_FALSE(builder.Build(asset, ctx).IsOk());

    RemoveTree();
}
