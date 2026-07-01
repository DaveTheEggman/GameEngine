/// Draconic::Render — the `:taa` partition.
///
/// Temporal anti-aliasing resolve (ported from Sedulous taa.frag.hlsl). Blends the current jittered
/// HDR frame with the reprojected history: closest-depth motion selection, a tone-weighted 3x3
/// neighborhood AABB clip-to-center (kills fireflies + ghosting), and a luminance-adaptive blend
/// (stable when matched, responsive on change). Runs in linear HDR after the scene is composed
/// (opaque+sky+transparent) and before bloom/tonemap. Per-view color-history ping-pong (persistent),
/// managed here; jitter is applied to the projection by the caller (RenderFrame).
///
/// (Depth-based disocclusion is a follow-up — the neighborhood clip carries most of it; adding it needs
/// a persisted previous-frame depth. Variance clip + Catmull-Rom history are the planned refinements.)

module;
#include "Core/Prelude.h"

export module draconic.render:taa;

import draconic.core;
import draconic.rhi;
import draconic.rendergraph;
import draconic.shaders;
import draconic.shaders.system;

using namespace draconic::core;
namespace rhi = draconic::rhi;

export namespace draconic::render {

// Fullscreen-triangle VS, top-origin uv (see BloomPass — the negative-viewport flip requires it so RT
// sampling stays oriented). All TAA inputs are sampled by this uv.
inline constexpr const char8_t* kTaaVS = u8R"(
struct VSOut { float4 pos : SV_Position; float2 uv : TEXCOORD0; };
VSOut main(uint vid : SV_VertexID) {
    VSOut o;
    float2 raw = float2((vid << 1) & 2, vid & 2);
    o.pos = float4(raw * 2.0 - 1.0, 0.0, 1.0);
    o.uv  = float2(raw.x, 1.0 - raw.y);
    return o;
}
)";

// Resolve. Ported from Sedulous taa.frag.hlsl (tone-weighted neighborhood clip-to-center + luma-adaptive
// blend). Outputs the resolved color (SV_Target0, consumed by bloom/tonemap) AND the next-frame history
// (SV_Target1). Params ride in push constants.
inline constexpr const char8_t* kTaaPS = u8R"(
Texture2D    CurrentColor  : register(t0, space0);
Texture2D    HistoryColor  : register(t1, space0);
Texture2D    MotionVectors : register(t2, space0);
Texture2D    DepthTexture  : register(t3, space0);
SamplerState PointSamp     : register(s0, space0);
SamplerState LinearSamp    : register(s1, space0);

struct TaaPush {
    float2 TexelSize;      // 1 / size
    float  BlendFactor;    // base history weight (0.95)
    float  HistoryValid;   // 0 = first frame (no history)
    float2 JitterOffset;   // (unused in the resolve; reserved)
    float2 PrevJitterOffset;
};
[[vk::push_constant]] TaaPush pc;

float Luminance(float3 c) { return dot(c, float3(0.2126, 0.7152, 0.0722)); }
float3 ToneWeight(float3 c)        { return c / (1.0 + Luminance(c)); }
float3 InverseToneWeight(float3 c) { return c / max(1.0 - Luminance(c), 1e-5); }

float3 ClipToAABB(float3 color, float3 aabbMin, float3 aabbMax) {
    float3 center  = (aabbMax + aabbMin) * 0.5;
    float3 extents = (aabbMax - aabbMin) * 0.5;
    float3 shift   = color - center;
    float3 absUnit = abs(shift / max(extents, 0.0001));
    float  maxUnit = max(max(absUnit.x, absUnit.y), absUnit.z);
    return maxUnit > 1.0 ? center + (shift / maxUnit) : color;
}

struct PSOut { float4 Color : SV_Target0; float4 History : SV_Target1; };

PSOut main(float4 pos : SV_Position, float2 uv : TEXCOORD0) {
    float3 current = CurrentColor.Sample(PointSamp, uv).rgb;

    // Closest depth in a 3x3 neighborhood -> stable motion-vector selection (reduces silhouette ghosting).
    float  closestDepth = 1.0;
    float2 closestUV    = uv;
    for (int y = -1; y <= 1; ++y) {
        for (int x = -1; x <= 1; ++x) {
            float2 s = uv + float2(x, y) * pc.TexelSize;
            float  d = DepthTexture.Sample(PointSamp, s).r;
            if (d < closestDepth) { closestDepth = d; closestUV = s; }
        }
    }
    float2 motion    = MotionVectors.Sample(PointSamp, closestUV).rg;
    float2 historyUV = uv - motion;

    PSOut o;
    // No valid history (first frame) or reprojection off-screen -> take current, seed history.
    if (pc.HistoryValid < 0.5 || any(historyUV < 0.0) || any(historyUV > 1.0)) {
        o.Color = float4(current, 1.0); o.History = float4(current, 1.0); return o;
    }

    float3 history = HistoryColor.Sample(LinearSamp, historyUV).rgb;

    // Tone-weighted 3x3 neighborhood AABB (fireflies carry less weight), clip history toward its center.
    float3 currentW = ToneWeight(current);
    float3 nMin = currentW, nMax = currentW;
    for (int ny = -1; ny <= 1; ++ny) {
        for (int nx = -1; nx <= 1; ++nx) {
            if (nx == 0 && ny == 0) { continue; }
            float3 sW = ToneWeight(CurrentColor.Sample(PointSamp, uv + float2(nx, ny) * pc.TexelSize).rgb);
            nMin = min(nMin, sW); nMax = max(nMax, sW);
        }
    }
    float3 historyW = ClipToAABB(ToneWeight(history), nMin, nMax);

    // Luminance-adaptive blend: high (stable) when cur/history match, low (responsive) when they diverge.
    float lum0 = Luminance(currentW), lum1 = Luminance(historyW);
    float lumaDiff = 1.0 - abs(lum0 - lum1) / max(lum0, max(lum1, 0.1));
    float blend = lerp(0.85, pc.BlendFactor, saturate(lumaDiff * lumaDiff));

    float3 result = InverseToneWeight(lerp(currentW, historyW, blend));
    o.Color = float4(result, 1.0); o.History = float4(result, 1.0); return o;
}
)";

// Owns the TAA resolve pipeline + per-view color-history ping-pong. One per renderer.
class TaaPass {
public:
    static constexpr rhi::TextureFormat kHistoryFormat = rhi::TextureFormat::RGBA16Float;
    static constexpr u32 kMaxViews = 8;

    TaaPass(rhi::Device& device, shaders::ShaderSystem& shaders) noexcept
        : m_device(&device), m_shaders(&shaders) {}
    ~TaaPass() { Shutdown(); }
    TaaPass(const TaaPass&) = delete;
    TaaPass& operator=(const TaaPass&) = delete;

    Status Initialize() {
        m_shaders->RegisterSource(u8"taa", shaders::ShaderStage::Vertex,   kTaaVS);
        m_shaders->RegisterSource(u8"taa", shaders::ShaderStage::Fragment, kTaaPS);

        // set 0: current(t0) history(t1) motion(t2) depth(t3) + point(s0) linear(s1).
        rhi::BindGroupLayoutEntry e[] = {
            rhi::BindGroupLayoutEntry::SampledTexture(0, rhi::ShaderStage::Fragment),
            rhi::BindGroupLayoutEntry::SampledTexture(1, rhi::ShaderStage::Fragment),
            rhi::BindGroupLayoutEntry::SampledTexture(2, rhi::ShaderStage::Fragment),
            rhi::BindGroupLayoutEntry::SampledTexture(3, rhi::ShaderStage::Fragment),
            rhi::BindGroupLayoutEntry::Sampler(0, rhi::ShaderStage::Fragment),
            rhi::BindGroupLayoutEntry::Sampler(1, rhi::ShaderStage::Fragment),
        };
        rhi::BindGroupLayoutDesc ld{}; ld.entries = Span<const rhi::BindGroupLayoutEntry>{ e, 6 };
        if (!m_device->CreateBindGroupLayout(ld, m_layout).IsOk()) { return Status{ ErrorCode::Unknown }; }

        rhi::BindGroupLayout* layouts[] = { m_layout };
        rhi::PushConstantRange pc{}; pc.stages = rhi::ShaderStage::Fragment; pc.offset = 0; pc.size = sizeof(TaaPush);
        rhi::PipelineLayoutDesc pld{};
        pld.bindGroupLayouts = Span<rhi::BindGroupLayout* const>{ layouts, 1 };
        pld.pushConstantRanges = Span<const rhi::PushConstantRange>{ &pc, 1 };
        if (!m_device->CreatePipelineLayout(pld, m_pipelineLayout).IsOk()) { return Status{ ErrorCode::Unknown }; }

        auto sampler = [this](rhi::FilterMode f, rhi::Sampler*& out) {
            rhi::SamplerDesc s{}; s.minFilter = f; s.magFilter = f;
            s.addressU = rhi::AddressMode::ClampToEdge; s.addressV = rhi::AddressMode::ClampToEdge; s.addressW = rhi::AddressMode::ClampToEdge;
            return m_device->CreateSampler(s, out).IsOk();
        };
        if (!sampler(rhi::FilterMode::Nearest, m_pointSampler) || !sampler(rhi::FilterMode::Linear, m_linearSampler)) { return Status{ ErrorCode::Unknown }; }

        m_pipeline = MakePipeline();
        if (m_pipeline == nullptr) { return Status{ ErrorCode::Unknown }; }
        return Status{};
    }

    [[nodiscard]] rhi::TextureFormat HistoryFormat() const noexcept { return kHistoryFormat; }

    // Resolve TAA for one view: reads the jittered HDR (`current`), the previous history, the motion +
    // depth targets; writes the resolved HDR (a new transient, returned) and the next-frame history.
    // `blendFactor` ~0.95. Returns the resolved handle, or `current` unchanged if the view is invalid.
    [[nodiscard]] rendergraph::RGHandle DeclareTaa(rendergraph::RenderGraph& graph, rendergraph::RGHandle current,
                                                   rendergraph::RGHandle motion, rendergraph::RGHandle depth,
                                                   u32 viewIndex, u32 w, u32 h, f32 blendFactor) {
        if (viewIndex >= kMaxViews || w == 0 || h == 0) { return current; }
        ViewHistory& hist = m_views[viewIndex];
        if (!EnsureHistory(hist, w, h)) { return current; }

        const u32 cur = hist.cur, prev = cur ^ 1u;
        const rendergraph::RGHandle resolved = graph.CreateTransient(
            u8"taa.resolved", rendergraph::RGTextureDesc(kHistoryFormat, w, h));
        const rendergraph::RGHandle histPrev = graph.ImportTarget(u8"taa.histPrev", hist.tex[prev], hist.view[prev],
                                                                  rhi::ResourceState::ShaderRead, hist.state[prev]);
        hist.state[prev] = rhi::ResourceState::ShaderRead;
        const rendergraph::RGHandle histCur = graph.ImportTarget(u8"taa.histCur", hist.tex[cur], hist.view[cur],
                                                                 rhi::ResourceState::RenderTarget, hist.state[cur]);
        hist.state[cur] = rhi::ResourceState::RenderTarget;

        TaaPush push{};
        push.texelSize = Vec2{ 1.0f / static_cast<f32>(w), 1.0f / static_cast<f32>(h) };
        push.blendFactor = blendFactor;
        push.historyValid = hist.valid ? 1.0f : 0.0f;

        rhi::TextureView* histPrevView = hist.view[prev];
        graph.AddRenderPass(u8"taa", [this, &graph, current, motion, depth, histPrev, resolved, histCur, histPrevView, push](rendergraph::PassBuilder& b) {
            b.SetColorTarget(0, resolved, rhi::LoadOp::Clear, rhi::StoreOp::Store, rhi::ClearColor::Black());
            b.SetColorTarget(1, histCur,  rhi::LoadOp::Clear, rhi::StoreOp::Store, rhi::ClearColor::Black());
            b.ReadTexture(current);
            b.ReadTexture(histPrev);   // last frame's history -> ShaderRead (ordered after its write)
            b.ReadTexture(motion);
            b.ReadTexture(depth);
            b.NeverCull();
            b.SetExecute([this, &graph, current, motion, depth, histPrevView, push](rhi::RenderPassEncoder& rp) {
                rhi::BindGroup* bg = EnsureBindGroup(graph.GetTextureView(current), histPrevView,
                                                     graph.GetTextureView(motion), graph.GetTextureView(depth),
                                                     graph.GetTextureGeneration(current));
                if (bg == nullptr) { return; }
                rp.SetPipeline(m_pipeline);
                rp.SetBindGroup(0, bg, Span<const u32>{});
                rp.SetPushConstants(rhi::ShaderStage::Fragment, 0, sizeof(TaaPush), &push);
                rp.Draw(3, 1, 0, 0);
            });
        });

        hist.cur = prev;   // this frame's output (cur) becomes next frame's history-to-read
        hist.valid = true;
        return resolved;
    }

private:
    struct TaaPush { Vec2 texelSize{}; f32 blendFactor = 0.95f; f32 historyValid = 0.0f; Vec2 jitter{}; Vec2 prevJitter{}; };

    struct ViewHistory {
        rhi::Texture*     tex[2]  = {};
        rhi::TextureView* view[2] = {};
        rhi::ResourceState state[2] = { rhi::ResourceState::Undefined, rhi::ResourceState::Undefined };
        u32  w = 0, h = 0, cur = 0;
        bool valid = false;
    };

    bool EnsureHistory(ViewHistory& hist, u32 w, u32 h) {
        if (hist.tex[0] != nullptr && hist.w == w && hist.h == h) { return true; }
        DestroyHistory(hist);
        for (u32 i = 0; i < 2; ++i) {
            rhi::TextureDesc td{};
            td.format = kHistoryFormat; td.width = w; td.height = h;
            td.usage = rhi::TextureUsage::RenderTarget | rhi::TextureUsage::Sampled; td.label = u8"taa.history";
            if (!m_device->CreateTexture(td, hist.tex[i]).IsOk()) { hist.tex[i] = nullptr; DestroyHistory(hist); return false; }
            rhi::TextureViewDesc vd{}; vd.format = kHistoryFormat; vd.dimension = rhi::TextureViewDimension::Texture2D;
            if (!m_device->CreateTextureView(hist.tex[i], vd, hist.view[i]).IsOk()) { hist.view[i] = nullptr; DestroyHistory(hist); return false; }
            hist.state[i] = rhi::ResourceState::Undefined;
        }
        hist.w = w; hist.h = h; hist.cur = 0; hist.valid = false;   // size changed -> history stale
        return true;
    }

    void DestroyHistory(ViewHistory& hist) {
        for (u32 i = 0; i < 2; ++i) {
            if (hist.view[i]) { m_device->DestroyTextureView(hist.view[i]); hist.view[i] = nullptr; }
            if (hist.tex[i])  { m_device->DestroyTexture(hist.tex[i]);       hist.tex[i]  = nullptr; }
        }
        hist.w = hist.h = 0; hist.valid = false;
    }

    rhi::RenderPipeline* MakePipeline() {
        rhi::ShaderModule* vs = m_shaders->GetVariant(u8"taa", shaders::ShaderStage::Vertex,   shaders::ShaderFlags::None);
        rhi::ShaderModule* ps = m_shaders->GetVariant(u8"taa", shaders::ShaderStage::Fragment, shaders::ShaderFlags::None);
        if (vs == nullptr || ps == nullptr) { return nullptr; }
        rhi::ColorTargetState targets[2] = {};
        targets[0].format = kHistoryFormat;   // resolved
        targets[1].format = kHistoryFormat;   // history
        rhi::FragmentState frag{}; frag.shader = rhi::ProgrammableStage{ ps, u8"main", rhi::ShaderStage::Fragment };
        frag.targets = Span<const rhi::ColorTargetState>{ targets, 2 };
        rhi::RenderPipelineDesc pd{};
        pd.layout = m_pipelineLayout;
        pd.vertex.shader = rhi::ProgrammableStage{ vs, u8"main", rhi::ShaderStage::Vertex };
        pd.fragment = frag;
        pd.primitive.topology = rhi::PrimitiveTopology::TriangleList;
        pd.primitive.cullMode = rhi::CullMode::None;
        pd.label = u8"taa";
        rhi::RenderPipeline* p = nullptr;
        if (!m_device->CreateRenderPipeline(pd, p).IsOk()) { return nullptr; }
        return p;
    }

    // Bind group over the 4 inputs, cached by (current view, generation) — the transients are pooled with
    // stable generations, the history view is stable per size, so this rebuilds only on resize.
    rhi::BindGroup* EnsureBindGroup(rhi::TextureView* cur, rhi::TextureView* histPrev, rhi::TextureView* motion,
                                    rhi::TextureView* depth, u64 generation) {
        if (cur == nullptr || histPrev == nullptr || motion == nullptr || depth == nullptr) { return nullptr; }
        if (Entry* e = m_bindGroups.Find(histPrev)) {
            if (e->gen == generation && e->cur == cur && e->bg != nullptr) { return e->bg; }
            if (e->bg != nullptr) { m_device->DestroyBindGroup(e->bg); e->bg = nullptr; }
        }
        rhi::BindGroupEntry ent[] = {
            rhi::BindGroupEntry::TextureEntry(cur), rhi::BindGroupEntry::TextureEntry(histPrev),
            rhi::BindGroupEntry::TextureEntry(motion), rhi::BindGroupEntry::TextureEntry(depth),
            rhi::BindGroupEntry::SamplerEntry(m_pointSampler), rhi::BindGroupEntry::SamplerEntry(m_linearSampler),
        };
        rhi::BindGroupDesc bgd{}; bgd.layout = m_layout; bgd.entries = Span<const rhi::BindGroupEntry>{ ent, 6 };
        rhi::BindGroup* bg = nullptr;
        if (!m_device->CreateBindGroup(bgd, bg).IsOk()) { return nullptr; }
        m_bindGroups.InsertOrAssign(histPrev, Entry{ bg, cur, generation });
        return bg;
    }

    void Shutdown() {
        for (auto& kv : m_bindGroups) { if (kv.value.bg != nullptr) { m_device->DestroyBindGroup(kv.value.bg); } }
        m_bindGroups.Clear();
        for (u32 v = 0; v < kMaxViews; ++v) { DestroyHistory(m_views[v]); }
        if (m_pipeline != nullptr) { m_device->DestroyRenderPipeline(m_pipeline); m_pipeline = nullptr; }
        if (m_pointSampler != nullptr) { m_device->DestroySampler(m_pointSampler); m_pointSampler = nullptr; }
        if (m_linearSampler != nullptr) { m_device->DestroySampler(m_linearSampler); m_linearSampler = nullptr; }
        if (m_pipelineLayout != nullptr) { m_device->DestroyPipelineLayout(m_pipelineLayout); m_pipelineLayout = nullptr; }
        if (m_layout != nullptr) { m_device->DestroyBindGroupLayout(m_layout); m_layout = nullptr; }
    }

    struct Entry { rhi::BindGroup* bg = nullptr; rhi::TextureView* cur = nullptr; u64 gen = 0; };

    rhi::Device*           m_device;
    shaders::ShaderSystem* m_shaders;
    rhi::BindGroupLayout*  m_layout = nullptr;
    rhi::PipelineLayout*   m_pipelineLayout = nullptr;
    rhi::RenderPipeline*   m_pipeline = nullptr;
    rhi::Sampler*          m_pointSampler = nullptr;
    rhi::Sampler*          m_linearSampler = nullptr;
    ViewHistory            m_views[kMaxViews];
    HashMap<rhi::TextureView*, Entry> m_bindGroups;
};

} // namespace draconic::render
