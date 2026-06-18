#include <doctest/doctest.h>

#include "Core/Prelude.h"

import raptor.core;
import raptor.rhi;
import raptor.rendergraph;

using namespace raptor::core;
using namespace raptor::rendergraph;
namespace rhi = raptor::rhi;

TEST_CASE("rendergraph.types: handles and validity")
{
    CHECK_FALSE(RGHandle::Invalid().IsValid());
    CHECK_FALSE(PassHandle::Invalid().IsValid());
    CHECK(RGHandle::Invalid() == RGHandle::Invalid());
    CHECK(RGHandle{ 3, 1 } != RGHandle{ 3, 2 });   // generation matters
    CHECK(RGHandle{ 5, 0 }.IsValid());
}

TEST_CASE("rendergraph.types: access read/write classification + state mapping")
{
    CHECK(IsRead(RGAccessType::ReadTexture));
    CHECK_FALSE(IsWrite(RGAccessType::ReadTexture));
    CHECK(IsWrite(RGAccessType::WriteColorTarget));
    CHECK(IsRead(RGAccessType::ReadWriteStorage));
    CHECK(IsWrite(RGAccessType::ReadWriteStorage));

    CHECK(ToResourceState(RGAccessType::WriteColorTarget) == rhi::ResourceState::RenderTarget);
    CHECK(ToResourceState(RGAccessType::ReadTexture) == rhi::ResourceState::ShaderRead);
    CHECK(ToResourceState(RGAccessType::WriteDepthTarget) == rhi::ResourceState::DepthStencilWrite);
    CHECK(ToResourceState(RGAccessType::ReadWriteStorage)
          == (rhi::ResourceState::ShaderWrite | rhi::ResourceState::ShaderRead));
}

TEST_CASE("rendergraph.types: subresource overlap")
{
    CHECK(RGSubresourceRange::All().IsAll());

    // Different array layers don't overlap.
    RGSubresourceRange layer0{ 0, 1, 0, 1 };
    RGSubresourceRange layer1{ 0, 1, 1, 1 };
    CHECK_FALSE(layer0.Overlaps(layer1));
    CHECK(layer0.Overlaps(layer0));

    // "All" overlaps any specific subresource.
    CHECK(RGSubresourceRange::All().Overlaps(layer1, 4, 4));
}

TEST_CASE("rendergraph.descriptors: size resolve + RHI desc conversion")
{
    RGTextureDesc desc;
    desc.format = rhi::TextureFormat::RGBA8Unorm;
    desc.sizeMode = SizeMode::HalfSize;
    desc.Resolve(1280, 720);
    CHECK(desc.width == 640u);
    CHECK(desc.height == 360u);

    const rhi::TextureDesc rhiDesc = desc.ToTextureDesc(u"gbuffer");
    CHECK(rhiDesc.format == rhi::TextureFormat::RGBA8Unorm);
    CHECK(rhiDesc.width == 640u);
    CHECK(rhiDesc.height == 360u);
    CHECK(rhiDesc.mipLevelCount == 1u);

    RGTextureDesc custom;
    custom.sizeMode = SizeMode::Custom;
    custom.width = 256; custom.height = 256;
    custom.Resolve(1280, 720);     // custom is untouched
    CHECK(custom.width == 256u);
}

TEST_CASE("rendergraph.persistent: ping-pong swap")
{
    // Fake non-null texture pointers (we only test index bookkeeping, no GPU).
    auto* a = reinterpret_cast<rhi::Texture*>(0x10);
    auto* b = reinterpret_cast<rhi::Texture*>(0x20);
    auto* va = reinterpret_cast<rhi::TextureView*>(0x30);
    auto* vb = reinterpret_cast<rhi::TextureView*>(0x40);

    PersistentResource single(a, va);
    CHECK_FALSE(single.IsPingPong());
    CHECK(single.CurrentTexture() == a);
    CHECK(single.PreviousTexture() == a);   // no history for single
    single.Swap();                          // no-op
    CHECK(single.CurrentTexture() == a);

    PersistentResource pp(a, b, va, vb);
    CHECK(pp.IsPingPong());
    CHECK(pp.CurrentTexture() == a);
    CHECK(pp.PreviousTexture() == b);
    pp.Swap();
    CHECK(pp.CurrentTexture() == b);
    CHECK(pp.PreviousTexture() == a);
}

TEST_CASE("rendergraph.resource: tracking + totals from descriptor")
{
    RenderGraphResource res(u"gbuffer", RGResourceType::Texture, RGResourceLifetime::Transient);
    res.textureDesc.mipLevelCount = 4;
    res.textureDesc.arrayLayerCount = 6;

    // No GPU texture allocated -> totals come from the descriptor.
    CHECK(res.TotalMipLevels() == 4u);
    CHECK(res.TotalArrayLayers() == 6u);

    res.refCount = 3;
    res.firstUsePass = 2;
    res.ResetTracking();
    CHECK(res.refCount == 0);
    CHECK(res.firstUsePass == -1);
    CHECK_FALSE(res.firstWriter.IsValid());
    CHECK_FALSE(res.finalState.HasValue());
}

TEST_CASE("rendergraph.state_tracker: uniform fast path, divergence, collapse")
{
    using RS = rhi::ResourceState;
    SubresourceStateTracker t(4, 2, RS::Undefined);   // 4 mips x 2 layers
    CHECK(t.IsUniform());
    CHECK(t.GetState(0, 0) == RS::Undefined);

    // Whole-resource set stays uniform.
    t.SetState(RGSubresourceRange::All(), RS::ShaderRead);
    CHECK(t.IsUniform());
    CHECK(t.GetState(3, 1) == RS::ShaderRead);

    // Diverging one subresource materializes per-subresource storage.
    t.SetState(0, 1, 0, 1, RS::RenderTarget);   // mip0, layer0
    CHECK_FALSE(t.IsUniform());
    CHECK(t.GetState(0, 0) == RS::RenderTarget);
    CHECK(t.GetState(1, 0) == RS::ShaderRead);

    Array<RS> snapshot = t.CopyStates();
    CHECK(snapshot.Size() == 8u);

    // SetAll collapses back to uniform.
    t.SetAll(RS::ShaderRead);
    CHECK(t.IsUniform());

    // Restoring the snapshot reproduces the non-uniform layout.
    SubresourceStateTracker restored(4, 2, RS::Undefined);
    restored.InitFromStates(snapshot, RS::Undefined);
    CHECK_FALSE(restored.IsUniform());
    CHECK(restored.GetState(0, 0) == RS::RenderTarget);
    CHECK(restored.GetState(2, 1) == RS::ShaderRead);

    // Re-converging all subresources collapses to uniform.
    restored.SetState(0, 1, 0, 1, RS::ShaderRead);
    CHECK(restored.IsUniform());
}
