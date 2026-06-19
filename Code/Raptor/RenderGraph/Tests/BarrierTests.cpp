// Ported from Sedulous.RenderGraph.Tests/BarrierTests.bf — validates the
// BarrierSolver directly via a recording mock encoder and plain RHI textures
// (rhi::Texture is concrete, so no texture mock is needed; its pointer identity
// is what the solver keys on).

#include <doctest/doctest.h>

#include "Core/Prelude.h"

import raptor.core;
import raptor.rhi;
import raptor.rendergraph;

using namespace raptor::core;
using namespace raptor::rendergraph;
namespace rhi = raptor::rhi;
using RS = rhi::ResourceState;
using AT = RGAccessType;

namespace
{
    // Records emitted barriers; stubs the rest of the CommandEncoder interface.
    class MockEncoder final : public rhi::CommandEncoder
    {
    public:
        Array<rhi::TextureBarrier> textureBarriers;
        Array<rhi::BufferBarrier> bufferBarriers;

        void Barrier(const rhi::BarrierGroup& group) override
        {
            for (usize i = 0; i < group.textureBarriers.Size(); ++i) { textureBarriers.PushBack(group.textureBarriers[i]); }
            for (usize i = 0; i < group.bufferBarriers.Size(); ++i) { bufferBarriers.PushBack(group.bufferBarriers[i]); }
        }

        rhi::RenderPassEncoder* BeginRenderPass(const rhi::RenderPassDesc&) override { return nullptr; }
        rhi::ComputePassEncoder* BeginComputePass(WideStringView) override { return nullptr; }
        void CopyBufferToBuffer(rhi::Buffer*, u64, rhi::Buffer*, u64, u64) override {}
        void CopyBufferToTexture(rhi::Buffer*, rhi::Texture*, const rhi::BufferTextureCopyRegion&) override {}
        void CopyTextureToBuffer(rhi::Texture*, rhi::Buffer*, const rhi::BufferTextureCopyRegion&) override {}
        void CopyTextureToTexture(rhi::Texture*, rhi::Texture*, const rhi::TextureCopyRegion&) override {}
        void Blit(rhi::Texture*, rhi::Texture*) override {}
        void GenerateMipmaps(rhi::Texture*) override {}
        void ResolveTexture(rhi::Texture*, rhi::Texture*) override {}
        void ResetQuerySet(rhi::QuerySet*, u32, u32) override {}
        void WriteTimestamp(rhi::QuerySet*, u32) override {}
        void ResolveQuerySet(rhi::QuerySet*, u32, u32, rhi::Buffer*, u64) override {}
        void BeginDebugLabel(WideStringView, f32, f32, f32, f32) override {}
        void EndDebugLabel() override {}
        void InsertDebugLabel(WideStringView, f32, f32, f32, f32) override {}
        rhi::CommandBuffer* Finish() override { return nullptr; }
    };

    rhi::Texture MakeTexture(RS initialState, u32 mips = 1, u32 layers = 1)
    {
        rhi::Texture tex;
        tex.desc.mipLevelCount = mips;
        tex.desc.arrayLayerCount = layers;
        tex.initialState = initialState;
        return tex;
    }

    RGResourceAccess Access(u32 index, AT type, RGSubresourceRange sub = {})
    {
        return RGResourceAccess{ RGHandle{ index, 0 }, type, sub };
    }
}

TEST_CASE("barriers: same texture via two handles emits one transition")
{
    BarrierSolver solver;
    MockEncoder encoder;
    rhi::Texture shadow = MakeTexture(RS::Undefined);

    RenderGraphResource res0(u"ShadowWrite", RGResourceType::Texture, RGResourceLifetime::Imported);
    res0.texture = &shadow;
    RenderGraphResource res1(u"ShadowRead", RGResourceType::Texture, RGResourceLifetime::Imported);
    res1.texture = &shadow; // same GPU texture
    RenderGraphResource* resources[] = { &res0, &res1 };
    const Span<RenderGraphResource* const> span(resources, 2);

    solver.Reset(span);

    RenderGraphPass writePass(u"ShadowPass", RGPassType::Render);
    writePass.accesses.PushBack(Access(0, AT::WriteDepthTarget));
    solver.EmitBarriers(writePass, span, encoder);
    encoder.textureBarriers.Clear();

    RenderGraphPass readPass(u"ForwardPass", RGPassType::Render);
    readPass.accesses.PushBack(Access(1, AT::ReadTexture));
    solver.EmitBarriers(readPass, span, encoder);

    REQUIRE(encoder.textureBarriers.Size() == 1u);
    CHECK(encoder.textureBarriers[0].oldState == RS::DepthStencilWrite);
    CHECK(encoder.textureBarriers[0].newState == RS::ShaderRead);
    CHECK(encoder.textureBarriers[0].texture == &shadow);
}

TEST_CASE("barriers: single handle read-after-write")
{
    BarrierSolver solver;
    MockEncoder encoder;
    rhi::Texture tex = MakeTexture(RS::Undefined);

    RenderGraphResource res(u"Color", RGResourceType::Texture, RGResourceLifetime::Transient);
    res.texture = &tex;
    RenderGraphResource* resources[] = { &res };
    const Span<RenderGraphResource* const> span(resources, 1);
    solver.Reset(span);

    RenderGraphPass writePass(u"Writer", RGPassType::Render);
    writePass.accesses.PushBack(Access(0, AT::WriteColorTarget));
    solver.EmitBarriers(writePass, span, encoder);
    encoder.textureBarriers.Clear();

    RenderGraphPass readPass(u"Reader", RGPassType::Render);
    readPass.accesses.PushBack(Access(0, AT::ReadTexture));
    solver.EmitBarriers(readPass, span, encoder);

    REQUIRE(encoder.textureBarriers.Size() == 1u);
    CHECK(encoder.textureBarriers[0].oldState == RS::RenderTarget);
    CHECK(encoder.textureBarriers[0].newState == RS::ShaderRead);
}

TEST_CASE("barriers: compute write then render read")
{
    BarrierSolver solver;
    MockEncoder encoder;
    rhi::Texture tex = MakeTexture(RS::Undefined);

    RenderGraphResource res(u"Volume", RGResourceType::Texture, RGResourceLifetime::Imported);
    res.texture = &tex;
    RenderGraphResource* resources[] = { &res };
    const Span<RenderGraphResource* const> span(resources, 1);
    solver.Reset(span);

    RenderGraphPass computePass(u"Compute", RGPassType::Compute);
    computePass.accesses.PushBack(Access(0, AT::WriteStorage));
    solver.EmitBarriers(computePass, span, encoder);
    encoder.textureBarriers.Clear();

    RenderGraphPass renderPass(u"Render", RGPassType::Render);
    renderPass.accesses.PushBack(Access(0, AT::ReadTexture));
    solver.EmitBarriers(renderPass, span, encoder);

    REQUIRE(encoder.textureBarriers.Size() == 1u);
    CHECK(encoder.textureBarriers[0].oldState == RS::ShaderWrite);
    CHECK(encoder.textureBarriers[0].newState == RS::ShaderRead);
}

TEST_CASE("barriers: final transition uses texture-keyed state")
{
    BarrierSolver solver;
    MockEncoder encoder;
    rhi::Texture tex = MakeTexture(RS::Undefined);

    RenderGraphResource res(u"Backbuffer", RGResourceType::Texture, RGResourceLifetime::Imported);
    res.texture = &tex;
    res.finalState = RS::Present;
    RenderGraphResource* resources[] = { &res };
    const Span<RenderGraphResource* const> span(resources, 1);
    solver.Reset(span);

    RenderGraphPass pass(u"FinalBlit", RGPassType::Render);
    pass.accesses.PushBack(Access(0, AT::WriteColorTarget));
    solver.EmitBarriers(pass, span, encoder);
    encoder.textureBarriers.Clear();

    solver.EmitFinalTransitions(span, encoder);

    REQUIRE(encoder.textureBarriers.Size() == 1u);
    CHECK(encoder.textureBarriers[0].oldState == RS::RenderTarget);
    CHECK(encoder.textureBarriers[0].newState == RS::Present);
}

TEST_CASE("barriers: none when already in the correct state")
{
    BarrierSolver solver;
    MockEncoder encoder;
    rhi::Texture tex = MakeTexture(RS::Undefined);

    RenderGraphResource res(u"Tex", RGResourceType::Texture, RGResourceLifetime::Imported);
    res.texture = &tex;
    res.lastKnownState = RS::ShaderRead;
    RenderGraphResource* resources[] = { &res };
    const Span<RenderGraphResource* const> span(resources, 1);
    solver.Reset(span);

    RenderGraphPass pass(u"Reader", RGPassType::Render);
    pass.accesses.PushBack(Access(0, AT::ReadTexture));
    solver.EmitBarriers(pass, span, encoder);

    CHECK(encoder.textureBarriers.Size() == 0u);
}

TEST_CASE("barriers: per-layer writes emit individual barriers")
{
    BarrierSolver solver;
    MockEncoder encoder;
    rhi::Texture tex = MakeTexture(RS::Undefined, 1, 4); // 4 cascades

    RenderGraphResource res(u"ShadowArray", RGResourceType::Texture, RGResourceLifetime::Imported);
    res.texture = &tex;
    res.lastKnownState = RS::ShaderRead;
    RenderGraphResource* resources[] = { &res };
    const Span<RenderGraphResource* const> span(resources, 1);
    solver.Reset(span);

    RenderGraphPass pass0(u"Cascade0", RGPassType::Render);
    pass0.accesses.PushBack(Access(0, AT::WriteDepthTarget, RGSubresourceRange{ 0, 1, 0, 1 }));
    solver.EmitBarriers(pass0, span, encoder);
    REQUIRE(encoder.textureBarriers.Size() == 1u);
    CHECK(encoder.textureBarriers[0].oldState == RS::ShaderRead);
    CHECK(encoder.textureBarriers[0].newState == RS::DepthStencilWrite);
    CHECK(encoder.textureBarriers[0].baseArrayLayer == 0u);
    CHECK(encoder.textureBarriers[0].arrayLayerCount == 1u);
    encoder.textureBarriers.Clear();

    RenderGraphPass pass2(u"Cascade2", RGPassType::Render);
    pass2.accesses.PushBack(Access(0, AT::WriteDepthTarget, RGSubresourceRange{ 0, 1, 2, 1 }));
    solver.EmitBarriers(pass2, span, encoder);
    REQUIRE(encoder.textureBarriers.Size() == 1u);
    CHECK(encoder.textureBarriers[0].baseArrayLayer == 2u);
    CHECK(encoder.textureBarriers[0].arrayLayerCount == 1u);
}

TEST_CASE("barriers: non-uniform whole-resource read emits per-subresource")
{
    BarrierSolver solver;
    MockEncoder encoder;
    rhi::Texture tex = MakeTexture(RS::Undefined, 1, 2);

    RenderGraphResource res(u"Tex", RGResourceType::Texture, RGResourceLifetime::Imported);
    res.texture = &tex;
    res.lastKnownState = RS::ShaderRead;
    RenderGraphResource* resources[] = { &res };
    const Span<RenderGraphResource* const> span(resources, 1);
    solver.Reset(span);

    RenderGraphPass writePass(u"Writer", RGPassType::Render);
    writePass.accesses.PushBack(Access(0, AT::WriteColorTarget, RGSubresourceRange{ 0, 1, 0, 1 }));
    solver.EmitBarriers(writePass, span, encoder);
    encoder.textureBarriers.Clear();

    RenderGraphPass readPass(u"Reader", RGPassType::Render);
    readPass.accesses.PushBack(Access(0, AT::ReadTexture));
    solver.EmitBarriers(readPass, span, encoder);

    REQUIRE(encoder.textureBarriers.Size() == 1u); // only layer 0 transitions
    CHECK(encoder.textureBarriers[0].oldState == RS::RenderTarget);
    CHECK(encoder.textureBarriers[0].newState == RS::ShaderRead);
    CHECK(encoder.textureBarriers[0].baseArrayLayer == 0u);
}

TEST_CASE("barriers: all layers written collapses to uniform")
{
    BarrierSolver solver;
    MockEncoder encoder;
    rhi::Texture tex = MakeTexture(RS::Undefined, 1, 2);

    RenderGraphResource res(u"Tex", RGResourceType::Texture, RGResourceLifetime::Imported);
    res.texture = &tex;
    res.lastKnownState = RS::ShaderRead;
    RenderGraphResource* resources[] = { &res };
    const Span<RenderGraphResource* const> span(resources, 1);
    solver.Reset(span);

    RenderGraphPass p0(u"W0", RGPassType::Render);
    p0.accesses.PushBack(Access(0, AT::WriteColorTarget, RGSubresourceRange{ 0, 1, 0, 1 }));
    solver.EmitBarriers(p0, span, encoder);
    encoder.textureBarriers.Clear();

    RenderGraphPass p1(u"W1", RGPassType::Render);
    p1.accesses.PushBack(Access(0, AT::WriteColorTarget, RGSubresourceRange{ 0, 1, 1, 1 }));
    solver.EmitBarriers(p1, span, encoder);
    encoder.textureBarriers.Clear();

    RenderGraphPass readPass(u"Read", RGPassType::Render);
    readPass.accesses.PushBack(Access(0, AT::ReadTexture));
    solver.EmitBarriers(readPass, span, encoder);

    REQUIRE(encoder.textureBarriers.Size() == 1u); // whole-resource fast path
    CHECK(encoder.textureBarriers[0].oldState == RS::RenderTarget);
    CHECK(encoder.textureBarriers[0].newState == RS::ShaderRead);
    CHECK(encoder.textureBarriers[0].mipLevelCount == 0xFFFFFFFFu);
    CHECK(encoder.textureBarriers[0].arrayLayerCount == 0xFFFFFFFFu);
}

TEST_CASE("barriers: per-mip different states")
{
    BarrierSolver solver;
    MockEncoder encoder;
    rhi::Texture tex = MakeTexture(RS::Undefined, 4, 1);

    RenderGraphResource res(u"Tex", RGResourceType::Texture, RGResourceLifetime::Imported);
    res.texture = &tex;
    res.lastKnownState = RS::Undefined;
    RenderGraphResource* resources[] = { &res };
    const Span<RenderGraphResource* const> span(resources, 1);
    solver.Reset(span);

    RenderGraphPass p0(u"WriteMip0", RGPassType::Render);
    p0.accesses.PushBack(Access(0, AT::WriteColorTarget, RGSubresourceRange{ 0, 1, 0, 1 }));
    solver.EmitBarriers(p0, span, encoder);
    encoder.textureBarriers.Clear();

    RenderGraphPass p1(u"WriteMip1", RGPassType::Render);
    p1.accesses.PushBack(Access(0, AT::WriteColorTarget, RGSubresourceRange{ 1, 1, 0, 1 }));
    solver.EmitBarriers(p1, span, encoder);

    REQUIRE(encoder.textureBarriers.Size() == 1u);
    CHECK(encoder.textureBarriers[0].oldState == RS::Undefined);
    CHECK(encoder.textureBarriers[0].newState == RS::RenderTarget);
    CHECK(encoder.textureBarriers[0].baseMipLevel == 1u);
    CHECK(encoder.textureBarriers[0].mipLevelCount == 1u);
}

TEST_CASE("barriers: readable-after-write is subresource aware")
{
    BarrierSolver solver;
    MockEncoder encoder;
    rhi::Texture tex = MakeTexture(RS::Undefined, 1, 4);

    RenderGraphResource res(u"Tex", RGResourceType::Texture, RGResourceLifetime::Imported);
    res.texture = &tex;
    res.lastKnownState = RS::ShaderRead;
    res.readableAfterWrite = true;
    RenderGraphResource* resources[] = { &res };
    const Span<RenderGraphResource* const> span(resources, 1);
    solver.Reset(span);

    RenderGraphPass writePass(u"Writer", RGPassType::Render);
    writePass.accesses.PushBack(Access(0, AT::WriteDepthTarget, RGSubresourceRange{ 0, 1, 1, 1 }));
    solver.EmitBarriers(writePass, span, encoder);
    encoder.textureBarriers.Clear();

    solver.EmitReadableAfterWriteBarriers(writePass, span, encoder);

    REQUIRE(encoder.textureBarriers.Size() == 1u);
    CHECK(encoder.textureBarriers[0].oldState == RS::DepthStencilWrite);
    CHECK(encoder.textureBarriers[0].newState == RS::ShaderRead);
    CHECK(encoder.textureBarriers[0].baseArrayLayer == 1u);
    CHECK(encoder.textureBarriers[0].arrayLayerCount == 1u);
}

TEST_CASE("barriers: final transition non-uniform emits per-subresource")
{
    BarrierSolver solver;
    MockEncoder encoder;
    rhi::Texture tex = MakeTexture(RS::Undefined, 1, 2);

    RenderGraphResource res(u"Swapchain", RGResourceType::Texture, RGResourceLifetime::Imported);
    res.texture = &tex;
    res.lastKnownState = RS::Undefined;
    res.finalState = RS::Present;
    RenderGraphResource* resources[] = { &res };
    const Span<RenderGraphResource* const> span(resources, 1);
    solver.Reset(span);

    RenderGraphPass writePass(u"Blit", RGPassType::Render);
    writePass.accesses.PushBack(Access(0, AT::WriteColorTarget, RGSubresourceRange{ 0, 1, 0, 1 }));
    solver.EmitBarriers(writePass, span, encoder);
    encoder.textureBarriers.Clear();

    solver.EmitFinalTransitions(span, encoder);

    REQUIRE(encoder.textureBarriers.Size() == 2u);
    bool foundLayer0 = false, foundLayer1 = false;
    for (usize i = 0; i < encoder.textureBarriers.Size(); ++i)
    {
        const rhi::TextureBarrier& b = encoder.textureBarriers[i];
        if (b.baseArrayLayer == 0 && b.oldState == RS::RenderTarget && b.newState == RS::Present) { foundLayer0 = true; }
        if (b.baseArrayLayer == 1 && b.oldState == RS::Undefined && b.newState == RS::Present) { foundLayer1 = true; }
    }
    CHECK(foundLayer0);
    CHECK(foundLayer1);
}

TEST_CASE("barriers: non-overlapping subresource access emits no false barrier")
{
    BarrierSolver solver;
    MockEncoder encoder;
    rhi::Texture tex = MakeTexture(RS::Undefined, 1, 4);

    RenderGraphResource res(u"Array", RGResourceType::Texture, RGResourceLifetime::Imported);
    res.texture = &tex;
    res.lastKnownState = RS::ShaderRead;
    RenderGraphResource* resources[] = { &res };
    const Span<RenderGraphResource* const> span(resources, 1);
    solver.Reset(span);

    RenderGraphPass writePass(u"WriteLayer0", RGPassType::Render);
    writePass.accesses.PushBack(Access(0, AT::WriteColorTarget, RGSubresourceRange{ 0, 1, 0, 1 }));
    solver.EmitBarriers(writePass, span, encoder);
    encoder.textureBarriers.Clear();

    RenderGraphPass readPass(u"ReadLayer2", RGPassType::Render);
    readPass.accesses.PushBack(Access(0, AT::ReadTexture, RGSubresourceRange{ 0, 1, 2, 1 }));
    solver.EmitBarriers(readPass, span, encoder);

    CHECK(encoder.textureBarriers.Size() == 0u);
}

TEST_CASE("barriers: persistent resource preserves per-subresource state")
{
    BarrierSolver solver;
    MockEncoder encoder;
    rhi::Texture tex = MakeTexture(RS::Undefined, 1, 2);

    RenderGraphResource res(u"Persistent", RGResourceType::Texture, RGResourceLifetime::Persistent);
    res.texture = &tex;
    res.persistentData = MakeUnique<PersistentResource>(DefaultAllocator(), &tex, static_cast<rhi::TextureView*>(nullptr));
    res.persistentData->firstFrame = false;
    res.persistentData->lastKnownState = RS::ShaderRead;
    RenderGraphResource* resources[] = { &res };
    const Span<RenderGraphResource* const> span(resources, 1);
    solver.Reset(span);

    RenderGraphPass writePass(u"Writer", RGPassType::Render);
    writePass.accesses.PushBack(Access(0, AT::WriteColorTarget, RGSubresourceRange{ 0, 1, 0, 1 }));
    solver.EmitBarriers(writePass, span, encoder);

    solver.UpdatePersistentStates(span);

    CHECK(res.persistentData->subresourceStates.Size() == 2u);
}

TEST_CASE("barriers: transient first access emits Undefined -> RenderTarget")
{
    BarrierSolver solver;
    MockEncoder encoder;
    rhi::Texture tex = MakeTexture(RS::Undefined);

    RenderGraphResource res(u"PipelineOutput", RGResourceType::Texture, RGResourceLifetime::Transient);
    res.texture = &tex;
    RenderGraphResource* resources[] = { &res };
    const Span<RenderGraphResource* const> span(resources, 1);
    solver.Reset(span);

    RenderGraphPass writePass(u"ForwardOpaque", RGPassType::Render);
    writePass.accesses.PushBack(Access(0, AT::WriteColorTarget));
    solver.EmitBarriers(writePass, span, encoder);

    REQUIRE(encoder.textureBarriers.Size() == 1u);
    CHECK(encoder.textureBarriers[0].oldState == RS::Undefined);
    CHECK(encoder.textureBarriers[0].newState == RS::RenderTarget);
}

TEST_CASE("barriers: transient reused across frames starts from Undefined")
{
    BarrierSolver solver;
    MockEncoder encoder;
    rhi::Texture tex = MakeTexture(RS::Undefined); // same pooled texture both frames

    // Frame 1
    {
        RenderGraphResource res(u"PipelineOutput", RGResourceType::Texture, RGResourceLifetime::Transient);
        res.texture = &tex;
        RenderGraphResource* resources[] = { &res };
        const Span<RenderGraphResource* const> span(resources, 1);
        solver.Reset(span);

        RenderGraphPass writePass(u"ForwardOpaque", RGPassType::Render);
        writePass.accesses.PushBack(Access(0, AT::WriteColorTarget));
        solver.EmitBarriers(writePass, span, encoder);
        encoder.textureBarriers.Clear();

        RenderGraphPass readPass(u"PostProcess", RGPassType::Render);
        readPass.accesses.PushBack(Access(0, AT::ReadTexture));
        solver.EmitBarriers(readPass, span, encoder);
        REQUIRE(encoder.textureBarriers.Size() == 1u);
        CHECK(encoder.textureBarriers[0].oldState == RS::RenderTarget);
        CHECK(encoder.textureBarriers[0].newState == RS::ShaderRead);
        encoder.textureBarriers.Clear();
    }

    // Frame 2: same texture, fresh resource — must restart from Undefined.
    {
        RenderGraphResource res(u"PipelineOutput", RGResourceType::Texture, RGResourceLifetime::Transient);
        res.texture = &tex;
        RenderGraphResource* resources[] = { &res };
        const Span<RenderGraphResource* const> span(resources, 1);
        solver.Reset(span);

        RenderGraphPass writePass(u"ForwardOpaque", RGPassType::Render);
        writePass.accesses.PushBack(Access(0, AT::WriteColorTarget));
        solver.EmitBarriers(writePass, span, encoder);

        REQUIRE(encoder.textureBarriers.Size() == 1u);
        CHECK(encoder.textureBarriers[0].oldState == RS::Undefined);
        CHECK(encoder.textureBarriers[0].newState == RS::RenderTarget);
    }
}
