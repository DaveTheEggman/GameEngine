// Ported from Sedulous.RenderGraph.Tests/DependencyTests.bf
#include <doctest/doctest.h>

#include "Core/Prelude.h"

import raptor.core;
import raptor.rhi;
import raptor.rendergraph;

using namespace raptor::core;
using namespace raptor::rendergraph;
namespace rhi = raptor::rhi;

namespace
{
    StringView PassName(RenderGraph& g, i32 orderSlot)
    {
        return g.Passes()[static_cast<usize>(g.ExecutionOrder()[static_cast<usize>(orderSlot)])]->name.AsView();
    }
}

TEST_CASE("rg.dep: reader depends on writer")
{
    RenderGraph graph(nullptr);
    graph.BeginFrame(0);
    const RGHandle tex = graph.CreateTransient(u"Tex", RGTextureDesc(rhi::TextureFormat::RGBA8Unorm));

    graph.AddRenderPass(u"Writer", [&](PassBuilder& b) {
        b.SetColorTarget(0, tex, rhi::LoadOp::Clear, rhi::StoreOp::Store);
        b.NeverCull();
    });
    graph.AddRenderPass(u"Reader", [&](PassBuilder& b) {
        b.ReadTexture(tex);
        b.NeverCull();
    });

    REQUIRE(graph.Compile().IsOk());
    REQUIRE(graph.ExecutionOrder().Size() == 2u);
    CHECK(PassName(graph, 0) == u"Writer");
}

TEST_CASE("rg.dep: multiple readers fan out, writer first")
{
    RenderGraph graph(nullptr);
    graph.BeginFrame(0);
    const RGHandle tex = graph.CreateTransient(u"Tex", RGTextureDesc(rhi::TextureFormat::RGBA8Unorm));

    graph.AddRenderPass(u"Writer", [&](PassBuilder& b) {
        b.SetColorTarget(0, tex, rhi::LoadOp::Clear, rhi::StoreOp::Store);
        b.NeverCull();
    });
    graph.AddRenderPass(u"ReaderA", [&](PassBuilder& b) { b.ReadTexture(tex); b.NeverCull(); });
    graph.AddRenderPass(u"ReaderB", [&](PassBuilder& b) { b.ReadTexture(tex); b.NeverCull(); });

    REQUIRE(graph.Compile().IsOk());
    REQUIRE(graph.ExecutionOrder().Size() == 3u);
    CHECK(PassName(graph, 0) == u"Writer");
}

TEST_CASE("rg.dep: subresource writes are independent, reader last")
{
    RenderGraph graph(nullptr);
    graph.BeginFrame(0);
    RGTextureDesc atlasDesc(rhi::TextureFormat::Depth32Float);
    atlasDesc.arrayLayerCount = 4;
    const RGHandle atlas = graph.CreateTransient(u"ShadowAtlas", atlasDesc);

    graph.AddRenderPass(u"Cascade0", [&](PassBuilder& b) {
        b.SetDepthTarget(atlas, rhi::LoadOp::Clear, rhi::StoreOp::Store, 1.0f, RGSubresourceRange{ 0, 1, 0, 1 });
        b.NeverCull();
    });
    graph.AddRenderPass(u"Cascade1", [&](PassBuilder& b) {
        b.SetDepthTarget(atlas, rhi::LoadOp::Clear, rhi::StoreOp::Store, 1.0f, RGSubresourceRange{ 0, 1, 1, 1 });
        b.NeverCull();
    });
    graph.AddRenderPass(u"Forward", [&](PassBuilder& b) { b.ReadTexture(atlas); b.NeverCull(); });

    REQUIRE(graph.Compile().IsOk());
    REQUIRE(graph.ExecutionOrder().Size() == 3u);
    CHECK(PassName(graph, 2) == u"Forward");
}

TEST_CASE("rg.dep: LoadOp on a color target creates a dependency on the writer")
{
    RenderGraph graph(nullptr);
    graph.BeginFrame(0);
    const RGHandle color = graph.CreateTransient(u"SceneColor", RGTextureDesc(rhi::TextureFormat::RGBA16Float));

    graph.AddRenderPass(u"ForwardOpaque", [&](PassBuilder& b) {
        b.SetColorTarget(0, color, rhi::LoadOp::Clear, rhi::StoreOp::Store);
        b.NeverCull();
    });
    graph.AddRenderPass(u"Terrain", [&](PassBuilder& b) {
        b.SetColorTarget(0, color, rhi::LoadOp::Load, rhi::StoreOp::Store);
        b.NeverCull();
    });

    REQUIRE(graph.Compile().IsOk());
    REQUIRE(graph.ExecutionOrder().Size() == 2u);
    CHECK(PassName(graph, 0) == u"ForwardOpaque");
    CHECK(PassName(graph, 1) == u"Terrain");
}

TEST_CASE("rg.dep: LoadOp on a depth target creates a dependency on the writer")
{
    RenderGraph graph(nullptr);
    graph.BeginFrame(0);
    const RGHandle depth = graph.CreateTransient(u"Depth", RGTextureDesc(rhi::TextureFormat::Depth32Float));

    graph.AddRenderPass(u"DepthPrepass", [&](PassBuilder& b) {
        b.SetDepthTarget(depth, rhi::LoadOp::Clear, rhi::StoreOp::Store);
        b.NeverCull();
    });
    graph.AddRenderPass(u"ForwardOpaque", [&](PassBuilder& b) {
        b.SetDepthTarget(depth, rhi::LoadOp::Load, rhi::StoreOp::Store);
        b.NeverCull();
    });

    REQUIRE(graph.Compile().IsOk());
    REQUIRE(graph.ExecutionOrder().Size() == 2u);
    CHECK(PassName(graph, 0) == u"DepthPrepass");
    CHECK(PassName(graph, 1) == u"ForwardOpaque");
}

TEST_CASE("rg.dep: writer chain orders correctly")
{
    RenderGraph graph(nullptr);
    graph.BeginFrame(0);
    const RGHandle tex = graph.CreateTransient(u"Tex", RGTextureDesc(rhi::TextureFormat::RGBA8Unorm));

    graph.AddRenderPass(u"Write1", [&](PassBuilder& b) {
        b.SetColorTarget(0, tex, rhi::LoadOp::Clear, rhi::StoreOp::Store);
        b.NeverCull();
    });
    graph.AddComputePass(u"Process", [&](PassBuilder& b) {
        b.ReadTexture(tex);
        b.WriteStorage(tex);
        b.NeverCull();
    });
    graph.AddRenderPass(u"FinalRead", [&](PassBuilder& b) { b.ReadTexture(tex); b.NeverCull(); });

    REQUIRE(graph.Compile().IsOk());
    REQUIRE(graph.ExecutionOrder().Size() == 3u);
    CHECK(PassName(graph, 0) == u"Write1");
    CHECK(PassName(graph, 1) == u"Process");
    CHECK(PassName(graph, 2) == u"FinalRead");
}
