// The first concrete exercise of the content/resource source->product split:
// author a TextureResource (+ pixel data stream), then bind it into a runtime
// Texture product through the ResourceManager.
#include <doctest/doctest.h>
#include "Core/Prelude.h"
#include "Core/Reflection/Reflect.h"
import raptor.core;
import raptor.vfs;
import raptor.content;
import raptor.resource;
import raptor.rhi;
import raptor.image;
import raptor.textures;
import raptor.textures.resource;

using namespace raptor::core;
using namespace raptor::vfs;
using namespace raptor::resource;
using namespace raptor::textures;
namespace rhi = raptor::rhi;
namespace img = raptor::image;

namespace
{
    void RemoveTree()
    {
        FileDelete(u8"raptor_texres_test_db/ui_icon.rasset");
        FileDelete(u8"raptor_texres_test_db/ui_icon.pixels.bin");
        RemoveDirectory(u8"raptor_texres_test_db");
    }
}

TEST_CASE("textures.resource: bind builds a Texture product from a TextureResource")
{
    RegisterTextureResource();

    RemoveTree();
    NativeFileSystem mount(u8"raptor_texres_test_db");

    // Author a 2x1 RGBA source + its pixel sidecar.
    const u8 pixels[8] = { 255, 0, 0, 255,  0, 255, 0, 255 };
    Guid id;
    {
        raptor::content::ContentDatabase db(mount);
        auto* inst = db.RootGroup()->CreateInstance(u8"ui_icon", TextureResource::StaticType());
        id = inst->Id();

        TextureResource r;
        r.SetupForUI();                 // ClampToEdge, no mips, linear
        r.imageWidth = 2;
        r.imageHeight = 1;
        r.imageFormat = img::PixelFormat::RGBA8;
        r.colorSpace = img::ImageColorSpace::Srgb;
        REQUIRE(inst->WriteObject(r).IsOk());
        REQUIRE(inst->WriteData(u8"pixels", Span<const byte>(reinterpret_cast<const byte*>(pixels), sizeof(pixels))).IsOk());
    }

    // Fresh DB (re-scan) + manager + factory.
    raptor::content::ContentDatabase db(mount);
    TextureFactory factory;
    ResourceManager manager(db);
    manager.AddFactory(&factory);

    Proxy<Texture> tex = manager.Bind<Texture>(id);
    REQUIRE(tex);
    CHECK(tex->width == 2u);
    CHECK(tex->height == 1u);
    // sRGB color imagery resolved to the sRGB GPU format.
    CHECK(tex->format == rhi::TextureFormat::RGBA8UnormSrgb);
    // UI preset carried through to the product.
    CHECK(tex->wrapU == TextureWrap::ClampToEdge);
    CHECK(tex->minFilter == TextureFilter::Linear);
    CHECK_FALSE(tex->generateMipmaps);

    // Pixel sidecar round-tripped into the product.
    REQUIRE(tex->Pixels().Size() == sizeof(pixels));
    CHECK(tex->Pixels().Data()[0] == 255);
    CHECK(tex->Pixels().Data()[4] == 0);
    CHECK(tex->Pixels().Data()[5] == 255);

    // The product yields a GPU-upload descriptor over its own pixels.
    TextureData d = tex->Descriptor();
    CHECK(d.width == 2u);
    CHECK(d.height == 1u);
    CHECK(d.format == rhi::TextureFormat::RGBA8UnormSrgb);
    CHECK(d.pixels == tex->Pixels().Data());
    CHECK(d.size == sizeof(pixels));

    // Caching: same id -> same handle.
    CHECK(manager.Bind<Texture>(id).Handle() == tex.Handle());

    RemoveTree();
}

TEST_CASE("textures.resource: metadata-only resource (no pixel stream) binds empty")
{
    RegisterTextureResource();

    RemoveTree();
    NativeFileSystem mount(u8"raptor_texres_test_db");
    Guid id;
    {
        raptor::content::ContentDatabase db(mount);
        auto* inst = db.RootGroup()->CreateInstance(u8"ui_icon", TextureResource::StaticType());
        id = inst->Id();
        TextureResource r;
        r.imageWidth = 4; r.imageHeight = 4;
        r.imageFormat = img::PixelFormat::R8;
        r.colorSpace = img::ImageColorSpace::Linear;
        REQUIRE(inst->WriteObject(r).IsOk());
    }

    raptor::content::ContentDatabase db(mount);
    TextureFactory factory;
    ResourceManager manager(db);
    manager.AddFactory(&factory);

    Proxy<Texture> tex = manager.Bind<Texture>(id);
    REQUIRE(tex);
    CHECK(tex->width == 4u);
    CHECK(tex->format == rhi::TextureFormat::R8Unorm); // linear R8
    CHECK(tex->Pixels().Size() == 0u);

    RemoveTree();
}
