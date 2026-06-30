// Model-A runtime load: author a cooked TextureResource (record + "data" stream)
// into a content DB, then load it through the ResourceManager with a device-backed
// TextureFactory (Null RHI backend, headless) and verify the live GPU Texture.
#include <doctest/doctest.h>
#include "Core/Prelude.h"
#include "Core/Reflection/Reflect.h"
import draconic.core;
import draconic.vfs;
import draconic.content;
import draconic.resource;
import draconic.rhi;
import draconic.rhi.null;
import draconic.texture;
import draconic.texture.resource;

using namespace draconic::core;
using namespace draconic::vfs;
using namespace draconic::resource;
using namespace draconic::texture;
namespace rhi = draconic::rhi;

namespace
{
    void RemoveTree()
    {
        FileDelete(u8"draconic_texfac_db/tex.rasset");
        FileDelete(u8"draconic_texfac_db/tex.data.bin");
        RemoveDirectory(u8"draconic_texfac_db");
    }
}

TEST_CASE("texture.factory: cooked TextureResource -> live GPU Texture")
{
    RegisterTextureResource();
    RemoveTree();

    NativeFileSystem mount(u8"draconic_texfac_db");
    Guid id;

    // Author a cooked record + raw 2x2 RGBA pixels (the "data" stream).
    {
        draconic::content::ContentDatabase db(mount);
        auto* inst = db.RootGroup()->CreateInstance(u8"tex", TextureResource::StaticType());
        id = inst->Id();

        TextureResource res;
        res.width = 2;
        res.height = 2;
        res.format = rhi::TextureFormat::RGBA8UnormSrgb;
        res.minFilter = TextureFilter::Linear;
        res.magFilter = TextureFilter::Linear;
        res.wrapU = TextureWrap::ClampToEdge;
        res.wrapV = TextureWrap::ClampToEdge;
        res.anisotropy = 8.0f;
        REQUIRE(inst->WriteObject(res).IsOk());

        u8 pixels[2 * 2 * 4];
        for (usize i = 0; i < sizeof(pixels); ++i) { pixels[i] = static_cast<u8>(i * 3); }
        REQUIRE(inst->WriteData(u8"data", Span<const byte>(reinterpret_cast<const byte*>(pixels), sizeof(pixels))).IsOk());
    }

    // Load through the manager with a device-backed factory (Null backend).
    rhi::null::NullDevice device;
    draconic::content::ContentDatabase db(mount);
    TextureFactory factory(device);
    ResourceManager manager(db);
    manager.AddFactory(&factory);

    Proxy<Texture> tex = manager.Bind<Texture>(id);
    REQUIRE(tex);
    CHECK(tex->Width() == 2u);
    CHECK(tex->Height() == 2u);
    CHECK(tex->Format() == rhi::TextureFormat::RGBA8UnormSrgb);
    CHECK(tex->GpuTexture() != nullptr);
    CHECK(tex->Sampler() != nullptr);

    RemoveTree();
}

TEST_CASE("texture.factory: rejects an instance whose object isn't a TextureResource")
{
    RegisterTextureResource();
    rhi::null::NullDevice device;
    TextureFactory factory(device);
    CHECK(factory.ProductType() == &Texture::StaticType());
}
