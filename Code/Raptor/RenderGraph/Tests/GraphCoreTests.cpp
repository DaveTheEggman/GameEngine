// Ported from Sedulous.RenderGraph.Tests/GraphCoreTests.bf
#include <doctest/doctest.h>

#include "Core/Prelude.h"

import raptor.core;
import raptor.rhi;
import raptor.rendergraph;

using namespace raptor::core;
using namespace raptor::rendergraph;
namespace rhi = raptor::rhi;

TEST_CASE("rg.graph: create transient returns a valid handle")
{
    RenderGraph graph(nullptr);
    graph.BeginFrame(0);
    const RGHandle handle = graph.CreateTransient(u"Test", RGTextureDesc(rhi::TextureFormat::RGBA8Unorm, SizeMode::FullSize));
    CHECK(handle.IsValid());
    CHECK(graph.ResourceCount() == 1u);
}

TEST_CASE("rg.graph: multiple resources get unique handles")
{
    RenderGraph graph(nullptr);
    graph.BeginFrame(0);
    const RGHandle h1 = graph.CreateTransient(u"A", RGTextureDesc(rhi::TextureFormat::RGBA8Unorm));
    const RGHandle h2 = graph.CreateTransient(u"B", RGTextureDesc(rhi::TextureFormat::Depth32Float));
    CHECK(h1 != h2);
    CHECK(graph.ResourceCount() == 2u);
}

TEST_CASE("rg.graph: get resource by name")
{
    RenderGraph graph(nullptr);
    graph.BeginFrame(0);
    const RGHandle h1 = graph.CreateTransient(u"SceneColor", RGTextureDesc(rhi::TextureFormat::RGBA8Unorm));
    CHECK(graph.GetResource(u"SceneColor") == h1);
    CHECK_FALSE(graph.GetResource(u"NonExistent").IsValid());
}

TEST_CASE("rg.graph: pass count is correct")
{
    RenderGraph graph(nullptr);
    graph.BeginFrame(0);
    const RGHandle color = graph.CreateTransient(u"Color", RGTextureDesc(rhi::TextureFormat::RGBA8Unorm));

    graph.AddRenderPass(u"Pass1", [&](PassBuilder& b) {
        b.SetColorTarget(0, color, rhi::LoadOp::Clear, rhi::StoreOp::Store);
        b.NeverCull();
    });
    graph.AddComputePass(u"Pass2", [](PassBuilder& b) { b.HasSideEffects(); });

    CHECK(graph.PassCount() == 2u);
}

TEST_CASE("rg.graph: set output size affects resolution")
{
    RenderGraph graph(nullptr);
    graph.SetOutputSize(1920, 1080);
    CHECK(graph.OutputWidth() == 1920u);
    CHECK(graph.OutputHeight() == 1080u);
}

TEST_CASE("rg.graph: import target with final state")
{
    RenderGraph graph(nullptr);
    graph.BeginFrame(0);
    const RGHandle handle = graph.ImportTarget(u"Backbuffer", nullptr, nullptr, rhi::ResourceState::Present);
    CHECK(handle.IsValid());
}

TEST_CASE("rg.graph: reset keeps persistent, drops transient")
{
    RenderGraph graph(nullptr);
    graph.BeginFrame(0);
    graph.RegisterPersistent(u"Shadow", nullptr, nullptr);
    graph.CreateTransient(u"Temp", RGTextureDesc(rhi::TextureFormat::RGBA8Unorm));

    graph.Reset();

    CHECK(graph.GetResource(u"Shadow").IsValid());
    CHECK_FALSE(graph.GetResource(u"Temp").IsValid());
}
