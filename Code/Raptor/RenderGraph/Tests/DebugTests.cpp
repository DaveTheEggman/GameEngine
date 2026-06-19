// Ported from Sedulous.RenderGraph.Tests/DebugTests.bf
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
    bool Contains(WideStringView hay, WideStringView needle)
    {
        if (needle.Size() > hay.Size()) { return false; }
        for (usize i = 0; i + needle.Size() <= hay.Size(); ++i)
        {
            if (hay.SubStr(i, needle.Size()) == needle) { return true; }
        }
        return false;
    }
}

TEST_CASE("rg.debug: ExportDOT produces valid syntax")
{
    RenderGraph graph(nullptr);
    graph.BeginFrame(0);
    const RGHandle color = graph.CreateTransient(u"SceneColor", RGTextureDesc(rhi::TextureFormat::RGBA8Unorm));
    const RGHandle depth = graph.CreateTransient(u"Depth", RGTextureDesc(rhi::TextureFormat::Depth32Float));

    graph.AddRenderPass(u"DepthPrepass", [&](PassBuilder& b) {
        b.SetDepthTarget(depth, rhi::LoadOp::Clear, rhi::StoreOp::Store);
        b.NeverCull();
    });
    graph.AddRenderPass(u"ForwardOpaque", [&](PassBuilder& b) {
        b.ReadTexture(depth);
        b.SetColorTarget(0, color, rhi::LoadOp::Clear, rhi::StoreOp::Store);
        b.NeverCull();
    });

    WideString dot;
    GraphDebug::ExportDOT(graph, dot);
    const WideStringView v = dot.AsView();
    CHECK(Contains(v, u"digraph"));
    CHECK(Contains(v, u"DepthPrepass"));
    CHECK(Contains(v, u"ForwardOpaque"));
    CHECK(Contains(v, u"SceneColor"));
    CHECK(Contains(v, u"}"));
}

TEST_CASE("rg.debug: ExportSummary includes counts")
{
    RenderGraph graph(nullptr);
    graph.SetOutputSize(1920, 1080);
    graph.BeginFrame(0);
    const RGHandle color = graph.CreateTransient(u"Color", RGTextureDesc(rhi::TextureFormat::RGBA8Unorm));
    graph.AddRenderPass(u"Pass1", [&](PassBuilder& b) {
        b.SetColorTarget(0, color, rhi::LoadOp::Clear, rhi::StoreOp::Store);
        b.NeverCull();
    });
    REQUIRE(graph.Compile().IsOk());

    WideString summary;
    GraphDebug::ExportSummary(graph, summary);
    const WideStringView v = summary.AsView();
    CHECK(Contains(v, u"1920x1080"));
    CHECK(Contains(v, u"Pass1"));
    CHECK(Contains(v, u"Render"));
}

TEST_CASE("rg.debug: DOT marks culled passes dashed")
{
    RenderGraph graph(nullptr);
    graph.BeginFrame(0);
    const RGHandle tex = graph.CreateTransient(u"Tex", RGTextureDesc(rhi::TextureFormat::RGBA8Unorm));
    graph.AddRenderPass(u"Culled", [&](PassBuilder& b) {
        b.SetColorTarget(0, tex, rhi::LoadOp::Clear, rhi::StoreOp::Store);
    });
    REQUIRE(graph.Compile().IsOk());

    WideString dot;
    GraphDebug::ExportDOT(graph, dot);
    CHECK(Contains(dot.AsView(), u"dashed"));
}
