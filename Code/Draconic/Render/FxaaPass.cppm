/// Draconic::Render - the `:fxaa` partition.
///
/// FXAA (Fast Approximate Anti-Aliasing), the TAA-OFF fallback AA. A single fullscreen LDR pass after
/// tonemap: perceptual-luma edge detect + directional edge search + sub-pixel blend (ported from the
/// SedulousEngine fxaa.frag quality variant). Only run when TAA is off - the two are never stacked
/// (locked decision) since TAA already resolves aliasing and FXAA on top would double-blur. Reads the
/// tonemapped LDR (a transient), writes the final target - both mapped to the view's sub-rect (so
/// split-screen views FXAA their own region), exactly like the tonemap.

module;
#include "Core/Prelude.h"

export module draconic.render:fxaa;

import draconic.core;
import draconic.rhi;
import draconic.rendergraph;
import draconic.shaders;
import draconic.shaders.system;

using namespace draconic::core;
namespace rhi = draconic::rhi;

export namespace draconic::render {

// Fullscreen-triangle VS, top-origin uv (matches the tonemap / post passes under the neg-viewport flip).
inline constexpr const char8_t* kFxaaVS = u8R"(
struct VSOut { float4 pos : SV_Position; float2 uv : TEXCOORD0; };
VSOut main(uint vid : SV_VertexID) {
    VSOut o;
    float2 raw = float2((vid << 1) & 2, vid & 2);
    o.pos = float4(raw * 2.0 - 1.0, 0.0, 1.0);
    o.uv  = float2(raw.x, 1.0 - raw.y);
    return o;
}
)";

// FXAA PS (quality variant). UvScale/UvOffset map the fullscreen uv to this view's sub-rect of the
// (full-size) tonemapped LDR; TexelSize is 1/full-size (the sub-rect is a contiguous region, so a
// full-texel neighbor step is a 1-pixel step within it - it only bleeds a texel across the split seam).
inline constexpr const char8_t* kFxaaPS = u8R"(
Texture2D    SceneColor : register(t0, space0);
SamplerState LinearSamp : register(s0, space0);
struct FxaaPush {
    float2 TexelSize;         // 1 / full target size
    float2 UvScale;           // fullscreen uv -> view sub-rect
    float2 UvOffset;
    float  SubpixelQuality;   // 0.75 default
    float  EdgeThreshold;     // 0.166 default
    float  EdgeThresholdMin;  // 0.0312 default (skip dark/flat)
    float  _pad;
};
[[vk::push_constant]] FxaaPush pc;

float Luma(float3 c) { return dot(c, float3(0.299, 0.587, 0.114)); }
float3 Fetch(float2 uv) { return SceneColor.SampleLevel(LinearSamp, uv, 0).rgb; }

float4 main(float4 pos : SV_Position, float2 rawUv : TEXCOORD0) : SV_Target {
    float2 ts = pc.TexelSize;
    float2 uv = pc.UvOffset + rawUv * pc.UvScale;   // this view's sub-rect

    float3 cC = Fetch(uv);
    float lC = Luma(cC);
    float lN = Luma(Fetch(uv + float2(0.0, -ts.y)));
    float lS = Luma(Fetch(uv + float2(0.0,  ts.y)));
    float lE = Luma(Fetch(uv + float2( ts.x, 0.0)));
    float lW = Luma(Fetch(uv + float2(-ts.x, 0.0)));

    float lMin = min(lC, min(min(lN, lS), min(lE, lW)));
    float lMax = max(lC, max(max(lN, lS), max(lE, lW)));
    float range = lMax - lMin;
    if (range < max(pc.EdgeThresholdMin, lMax * pc.EdgeThreshold)) { return float4(cC, 1.0); }

    float lNW = Luma(Fetch(uv + float2(-ts.x, -ts.y)));
    float lNE = Luma(Fetch(uv + float2( ts.x, -ts.y)));
    float lSW = Luma(Fetch(uv + float2(-ts.x,  ts.y)));
    float lSE = Luma(Fetch(uv + float2( ts.x,  ts.y)));

    // Sub-pixel aliasing amount.
    float lAvg = (lN + lS + lE + lW) * 0.25;
    float sub = saturate(abs(lAvg - lC) / range);
    sub = smoothstep(0.0, 1.0, sub); sub = sub * sub * pc.SubpixelQuality;

    // Edge orientation.
    float edgeH = abs(lNW + lNE - 2.0 * lN) + abs(lW + lE - 2.0 * lC) * 2.0 + abs(lSW + lSE - 2.0 * lS);
    float edgeV = abs(lNW + lSW - 2.0 * lW) + abs(lN + lS - 2.0 * lC) * 2.0 + abs(lNE + lSE - 2.0 * lE);
    bool horz = (edgeH >= edgeV);

    float stepLen = horz ? ts.y : ts.x;
    float lPos = horz ? lS : lE;
    float lNeg = horz ? lN : lW;
    float gPos = abs(lPos - lC);
    float gNeg = abs(lNeg - lC);

    float lLocalAvg;
    if (gPos >= gNeg) { lLocalAvg = 0.5 * (lC + lPos); }
    else { stepLen = -stepLen; lLocalAvg = 0.5 * (lC + lNeg); }

    float2 edgeUv = uv;
    if (horz) { edgeUv.y += stepLen * 0.5; } else { edgeUv.x += stepLen * 0.5; }
    float2 edgeStep = horz ? float2(ts.x, 0.0) : float2(0.0, ts.y);

    const float STEPS[12] = { 1.0, 1.0, 1.0, 1.0, 1.0, 1.5, 2.0, 2.0, 2.0, 2.0, 4.0, 8.0 };
    float2 uvP = edgeUv, uvN = edgeUv;
    float dP = 0.0, dN = 0.0;
    bool hitP = false, hitN = false;
    [unroll] for (int i = 0; i < 12; ++i) {
        if (!hitP) { uvP += edgeStep * STEPS[i]; dP = Luma(Fetch(uvP)) - lLocalAvg; hitP = abs(dP) >= gPos * 0.5; }
        if (!hitN) { uvN -= edgeStep * STEPS[i]; dN = Luma(Fetch(uvN)) - lLocalAvg; hitN = abs(dN) >= gNeg * 0.5; }
        if (hitP && hitN) { break; }
    }

    float distP = horz ? (uvP.x - uv.x) : (uvP.y - uv.y);
    float distN = horz ? (uv.x - uvN.x) : (uv.y - uvN.y);
    float distMin = min(distP, distN);
    float edgeLen = distP + distN;
    float edgeOff = -distMin / max(edgeLen, 1e-6) + 0.5;

    bool cSmaller = lC < lLocalAvg;
    bool correct = (((distP < distN) ? dP : dN) >= 0.0) != cSmaller;
    float finalOff = max(correct ? edgeOff : 0.0, sub);

    float2 finalUv = uv;
    if (horz) { finalUv.y += finalOff * stepLen; } else { finalUv.x += finalOff * stepLen; }
    return float4(Fetch(finalUv), 1.0);
}
)";

// Anti-aliases a tonemapped LDR transient into the final LDR target. Owns the fullscreen pipeline +
// per-(view,frame) bind groups over the (transient) source view. Declared after tonemap, when TAA is off.
class FxaaPass {
public:
    FxaaPass(rhi::Device& device, shaders::ShaderSystem& shaders, u32 framesInFlight) noexcept
        : m_device(&device), m_shaders(&shaders), m_framesInFlight(framesInFlight < 1 ? 1 : framesInFlight) {}
    ~FxaaPass() { Shutdown(); }
    FxaaPass(const FxaaPass&) = delete;
    FxaaPass& operator=(const FxaaPass&) = delete;

    Status Initialize() {
        m_shaders->RegisterSource(u8"fxaa", shaders::ShaderStage::Vertex,   kFxaaVS);
        m_shaders->RegisterSource(u8"fxaa", shaders::ShaderStage::Fragment, kFxaaPS);

        rhi::BindGroupLayoutEntry texEntry  = rhi::BindGroupLayoutEntry::SampledTexture(0, rhi::ShaderStage::Fragment);
        rhi::BindGroupLayoutEntry sampEntry = rhi::BindGroupLayoutEntry::Sampler(0, rhi::ShaderStage::Fragment);
        rhi::BindGroupLayoutEntry entries[] = { texEntry, sampEntry };
        rhi::BindGroupLayoutDesc ld{}; ld.entries = Span<const rhi::BindGroupLayoutEntry>{ entries, 2 };
        if (!m_device->CreateBindGroupLayout(ld, m_layout).IsOk()) { return Status{ ErrorCode::Unknown }; }

        rhi::BindGroupLayout* layouts[] = { m_layout };
        rhi::PushConstantRange pc{}; pc.stages = rhi::ShaderStage::Fragment; pc.offset = 0; pc.size = sizeof(f32) * 10;
        rhi::PipelineLayoutDesc pld{};
        pld.bindGroupLayouts = Span<rhi::BindGroupLayout* const>{ layouts, 1 };
        pld.pushConstantRanges = Span<const rhi::PushConstantRange>{ &pc, 1 };
        if (!m_device->CreatePipelineLayout(pld, m_pipelineLayout).IsOk()) { return Status{ ErrorCode::Unknown }; }

        rhi::SamplerDesc ss{};
        ss.minFilter = rhi::FilterMode::Linear; ss.magFilter = rhi::FilterMode::Linear;
        ss.addressU = rhi::AddressMode::ClampToEdge; ss.addressV = rhi::AddressMode::ClampToEdge; ss.addressW = rhi::AddressMode::ClampToEdge;
        ss.label = u8"fxaa.sampler";
        if (!m_device->CreateSampler(ss, m_sampler).IsOk()) { return Status{ ErrorCode::Unknown }; }
        return Status{};
    }

    // Declare the FXAA pass: read `src` (tonemapped LDR), write `ldr` (final), into the view's sub-rect.
    void DeclareFxaa(rendergraph::RenderGraph& graph, rendergraph::RGHandle src, rendergraph::RGHandle ldr,
                     bool clearColor, const rhi::ClearColor& clear, rhi::TextureFormat ldrFormat,
                     i32 vpX, i32 vpY, u32 vpW, u32 vpH, u32 frameIndex, u32 viewIndex,
                     Float2 texelSize, Float2 uvScale, Float2 uvOffset, f32 subpixelQuality = 0.75f) {
        rhi::RenderPipeline* pipeline = EnsurePipeline(ldrFormat);
        if (pipeline == nullptr) { return; }
        const u32 slot = (viewIndex % kMaxViews) * m_framesInFlight + (frameIndex % m_framesInFlight);
        const f32 push[10] = { texelSize.x, texelSize.y, uvScale.x, uvScale.y, uvOffset.x, uvOffset.y,
                               subpixelQuality, 0.166f, 0.0312f, 0.0f };

        const rhi::LoadOp load = clearColor ? rhi::LoadOp::Clear : rhi::LoadOp::Load;
        graph.AddRenderPass(u8"fxaa",
            [this, &graph, src, ldr, load, clear, vpX, vpY, vpW, vpH, pipeline, slot, push](rendergraph::PassBuilder& b) {
                b.SetColorTarget(0, ldr, load, rhi::StoreOp::Store, clear);
                b.ReadTexture(src);
                b.SetViewport(vpX, vpY, vpW, vpH);
                b.NeverCull();
                b.SetExecute([this, &graph, src, pipeline, slot, push](rhi::RenderPassEncoder& rp) {
                    rhi::BindGroup* bg = EnsureBindGroup(slot, graph.GetTextureView(src), graph.GetTextureGeneration(src));
                    if (bg == nullptr) { return; }
                    rp.SetPipeline(pipeline);
                    rp.SetBindGroup(0, bg, Span<const u32>{});
                    rp.SetPushConstants(rhi::ShaderStage::Fragment, 0, sizeof(push), push);
                    rp.Draw(3, 1, 0, 0);
                });
            });
    }

private:
    static constexpr u32 kMaxViews = 8;
    static constexpr u32 kMaxFramesInFlight = 8;
    static constexpr u32 kMaxSlots = kMaxViews * kMaxFramesInFlight;

    rhi::RenderPipeline* EnsurePipeline(rhi::TextureFormat fmt) {
        if (m_pipeline != nullptr && m_pipelineFormat == fmt) { return m_pipeline; }
        rhi::ShaderModule* vs = m_shaders->GetVariant(u8"fxaa", shaders::ShaderStage::Vertex,   shaders::ShaderFlags::None);
        rhi::ShaderModule* ps = m_shaders->GetVariant(u8"fxaa", shaders::ShaderStage::Fragment, shaders::ShaderFlags::None);
        if (vs == nullptr || ps == nullptr) { return nullptr; }
        if (m_pipeline != nullptr) { m_device->DestroyRenderPipeline(m_pipeline); m_pipeline = nullptr; }
        rhi::ColorTargetState color{}; color.format = fmt;
        rhi::FragmentState frag{}; frag.shader = rhi::ProgrammableStage{ ps, u8"main", rhi::ShaderStage::Fragment };
        frag.targets = Span<const rhi::ColorTargetState>{ &color, 1 };
        rhi::RenderPipelineDesc pd{};
        pd.layout = m_pipelineLayout;
        pd.vertex.shader = rhi::ProgrammableStage{ vs, u8"main", rhi::ShaderStage::Vertex };
        pd.fragment = frag;
        pd.primitive.topology = rhi::PrimitiveTopology::TriangleList;
        pd.primitive.cullMode = rhi::CullMode::None;
        pd.label = u8"fxaa";
        if (!m_device->CreateRenderPipeline(pd, m_pipeline).IsOk()) { m_pipeline = nullptr; return nullptr; }
        m_pipelineFormat = fmt;
        return m_pipeline;
    }

    rhi::BindGroup* EnsureBindGroup(u32 slot, rhi::TextureView* srcView, u64 generation) {
        if (slot >= kMaxSlots || srcView == nullptr) { return nullptr; }
        if (m_bindGroups[slot] != nullptr && m_bgViews[slot] == srcView && m_bgGen[slot] == generation) { return m_bindGroups[slot]; }
        if (m_bindGroups[slot] != nullptr) { m_device->DestroyBindGroup(m_bindGroups[slot]); m_bindGroups[slot] = nullptr; }
        rhi::BindGroupEntry entries[] = {
            rhi::BindGroupEntry::TextureEntry(srcView),
            rhi::BindGroupEntry::SamplerEntry(m_sampler),
        };
        rhi::BindGroupDesc bgd{}; bgd.layout = m_layout; bgd.entries = Span<const rhi::BindGroupEntry>{ entries, 2 };
        if (!m_device->CreateBindGroup(bgd, m_bindGroups[slot]).IsOk()) { m_bindGroups[slot] = nullptr; return nullptr; }
        m_bgViews[slot] = srcView;
        m_bgGen[slot]   = generation;
        return m_bindGroups[slot];
    }

    void Shutdown() {
        for (u32 i = 0; i < kMaxSlots; ++i) {
            if (m_bindGroups[i] != nullptr) { m_device->DestroyBindGroup(m_bindGroups[i]); m_bindGroups[i] = nullptr; }
        }
        if (m_pipeline != nullptr) { m_device->DestroyRenderPipeline(m_pipeline); m_pipeline = nullptr; }
        if (m_pipelineLayout != nullptr) { m_device->DestroyPipelineLayout(m_pipelineLayout); m_pipelineLayout = nullptr; }
        if (m_sampler != nullptr) { m_device->DestroySampler(m_sampler); m_sampler = nullptr; }
        if (m_layout != nullptr) { m_device->DestroyBindGroupLayout(m_layout); m_layout = nullptr; }
    }

    rhi::Device*           m_device;
    shaders::ShaderSystem* m_shaders;
    u32                    m_framesInFlight = 2;
    rhi::BindGroupLayout*  m_layout = nullptr;
    rhi::PipelineLayout*   m_pipelineLayout = nullptr;
    rhi::RenderPipeline*   m_pipeline = nullptr;
    rhi::TextureFormat     m_pipelineFormat = rhi::TextureFormat::Undefined;
    rhi::Sampler*          m_sampler = nullptr;
    rhi::BindGroup*        m_bindGroups[kMaxSlots] = {};
    rhi::TextureView*      m_bgViews[kMaxSlots] = {};
    u64                    m_bgGen[kMaxSlots] = {};
};

} // namespace draconic::render
