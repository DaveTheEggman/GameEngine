/// Draconic::Render — the `:tonemap` partition.
///
/// The HDR resolve: the forward pass renders linear HDR into a transient (RGBA16F); this fullscreen
/// pass reads it, applies exposure + a tonemap operator + the display OETF, and writes the LDR
/// target. Keeps the renderer in a strict linear working space (docs/design/renderer.md §12) — the
/// foundation IBL/post are designed against. CM1a uses a trivial clamp; CM1b swaps in AgX.

module;
#include "Core/Prelude.h"

export module draconic.render:tonemap;

import draconic.core;
import draconic.rhi;
import draconic.rendergraph;
import draconic.shaders;
import draconic.shaders.system;

using namespace draconic::core;
namespace rhi = draconic::rhi;

export namespace draconic::render {

// Fullscreen-triangle VS (no vertex buffer; positions from SV_VertexID). Emits a [0,1] uv for the
// (half-res, linearly-sampled) bloom composite; the HDR itself is read by texel Load.
inline constexpr const char8_t* kTonemapVS = u8R"(
struct VSOut { float4 pos : SV_Position; float2 uv : TEXCOORD0; };
VSOut main(uint vid : SV_VertexID) {
    VSOut o;
    float2 raw = float2((vid << 1) & 2, vid & 2);
    o.pos = float4(raw * 2.0 - 1.0, 0.0, 1.0);
    o.uv  = float2(raw.x, 1.0 - raw.y);   // top-origin (matches RT memory under the negative-viewport flip)
    return o;
}
)";

// AgX (Troy Sobotka's minimal approximation): a hue-preserving filmic display transform that
// rolls off bright saturated colors without the hue-shift of the ACES Narkowicz fit (design §12).
// Linear HDR in -> display-encoded LDR out (written straight to the UNORM target). Matrices are the
// GLSL minimal-AgX values transposed for HLSL mul(M, v). Exposure is fixed at 1.0 for now.
inline constexpr const char8_t* kTonemapPS = u8R"(
Texture2D<float4> Hdr       : register(t0, space0);
Texture2D<float4> Bloom     : register(t1, space0);
SamplerState      BloomSamp : register(s0, space0);
// UvScale/UvOffset map the fullscreen [0,1] uv to this view's sub-rect of the (full-size) HDR/bloom
// transients — so split-screen views resolve their own region instead of the whole target.
struct TonemapPush { float Exposure; float BloomIntensity; float2 UvScale; float2 UvOffset; };
[[vk::push_constant]] TonemapPush pc;

// 6th-order polynomial fit of the AgX log->display sigmoid.
float3 agxContrast(float3 x) {
    float3 x2 = x * x;
    float3 x4 = x2 * x2;
    return  15.5 * x4 * x2 - 40.14 * x4 * x + 31.96 * x4
          - 6.868 * x2 * x + 0.4298 * x2 + 0.1191 * x - 0.00232;
}

// The "punchy" AgX look (Blender's default): a contrast power + saturation boost around luma.
// Neutral base AgX is intentionally flat; this is what gives the expected filmic punch.
float3 agxLook(float3 val) {
    const float3 lw = float3(0.2126, 0.7152, 0.0722);
    float luma = dot(val, lw);
    val = pow(max(val, 0.0), float3(1.35, 1.35, 1.35));   // contrast (deeper shadows)
    return luma + 1.4 * (val - luma);                     // saturation
}

float4 main(float4 pos : SV_Position, float2 uv : TEXCOORD0) : SV_Target {
    // Sample HDR + bloom with the SAME (top-origin) uv, mapped to this view's sub-rect. Both via Sample
    // (Sedulous-style — mixing Load(pos) with Sample(uv) is what caused the mirrored bloom ghost).
    float2 st = pc.UvOffset + uv * pc.UvScale;
    float3 c = max(Hdr.SampleLevel(BloomSamp, st, 0).rgb, 0.0);
    c += Bloom.SampleLevel(BloomSamp, st, 0).rgb * max(pc.BloomIntensity, 0.0);   // additive bloom (linear HDR)
    c *= max(pc.Exposure, 0.0);   // linear exposure multiplier (scene setting)

    const float3x3 agxInset = float3x3(
        0.842479062253094, 0.0423282422610123, 0.0423756549057051,
        0.0784335999999992, 0.878468636469772, 0.0784336,
        0.0792237451477643, 0.0791661274605434, 0.879142973793104);
    const float3x3 agxOutset = float3x3(
        1.19687900512017,   -0.0528968517574562, -0.0529716355144438,
        -0.0980208811401368, 1.15190312990417,   -0.0980434501171241,
        -0.0990297440797205, -0.0989611768448433, 1.15107367264116);
    const float minEv = -12.47393;
    const float maxEv =  4.026069;

    float3 v = mul(agxInset, c);
    v = clamp(log2(max(v, 1e-10)), minEv, maxEv);
    v = (v - minEv) / (maxEv - minEv);     // normalize to [0,1]
    v = agxContrast(v);                    // sigmoid (output is display-encoded)
    v = agxLook(v);                        // punchy look (contrast + saturation)
    v = mul(agxOutset, v);
    return float4(saturate(v), 1.0);       // straight to the UNORM display target
}
)";

// Tonemaps an HDR transient into an LDR target. Owns the fullscreen pipeline + per-(view,frame)
// bind groups over the (transient) HDR view. Declared once per view, after that view's forward pass.
class TonemapPass {
public:
    TonemapPass(rhi::Device& device, shaders::ShaderSystem& shaders, u32 framesInFlight) noexcept
        : m_device(&device), m_shaders(&shaders), m_framesInFlight(framesInFlight < 1 ? 1 : framesInFlight) {}

    ~TonemapPass() { Shutdown(); }
    TonemapPass(const TonemapPass&) = delete;
    TonemapPass& operator=(const TonemapPass&) = delete;

    Status Initialize() {
        m_shaders->RegisterSource(u8"tonemap", shaders::ShaderStage::Vertex,   kTonemapVS);
        m_shaders->RegisterSource(u8"tonemap", shaders::ShaderStage::Fragment, kTonemapPS);

        // set 0: HDR (t0, texel Load) + bloom (t1, sampled) + a linear sampler (s0) for the bloom composite.
        rhi::BindGroupLayoutEntry hdrEntry   = rhi::BindGroupLayoutEntry::SampledTexture(0, rhi::ShaderStage::Fragment);
        rhi::BindGroupLayoutEntry bloomEntry = rhi::BindGroupLayoutEntry::SampledTexture(1, rhi::ShaderStage::Fragment);
        rhi::BindGroupLayoutEntry sampEntry  = rhi::BindGroupLayoutEntry::Sampler(0, rhi::ShaderStage::Fragment);
        rhi::BindGroupLayoutEntry entries[] = { hdrEntry, bloomEntry, sampEntry };
        rhi::BindGroupLayoutDesc ld{};
        ld.entries = Span<const rhi::BindGroupLayoutEntry>{ entries, 3 };
        if (!m_device->CreateBindGroupLayout(ld, m_layout).IsOk()) { return Status{ ErrorCode::Unknown }; }

        rhi::BindGroupLayout* layouts[] = { m_layout };
        rhi::PushConstantRange pc{}; pc.stages = rhi::ShaderStage::Fragment; pc.offset = 0; pc.size = sizeof(f32) * 6;   // exposure + bloom intensity + uvScale.xy + uvOffset.xy
        rhi::PipelineLayoutDesc pld{};
        pld.bindGroupLayouts = Span<rhi::BindGroupLayout* const>{ layouts, 1 };
        pld.pushConstantRanges = Span<const rhi::PushConstantRange>{ &pc, 1 };
        if (!m_device->CreatePipelineLayout(pld, m_pipelineLayout).IsOk()) { return Status{ ErrorCode::Unknown }; }

        rhi::SamplerDesc ss{};
        ss.minFilter = rhi::FilterMode::Linear; ss.magFilter = rhi::FilterMode::Linear;
        ss.addressU = rhi::AddressMode::ClampToEdge; ss.addressV = rhi::AddressMode::ClampToEdge; ss.addressW = rhi::AddressMode::ClampToEdge;
        ss.label = u8"tonemap.bloomSampler";
        if (!m_device->CreateSampler(ss, m_sampler).IsOk()) { return Status{ ErrorCode::Unknown }; }
        return Status{};
    }

    [[nodiscard]] rhi::TextureFormat HdrFormat() const noexcept { return kHdrFormat; }

    // Declare the tonemap pass: read `hdr`, write `ldr` (clearColor decides clear vs load), into the
    // view's viewport sub-rect. The execute builds/binds the HDR bind group (the view is a transient,
    // resolved at execute time) and draws a fullscreen triangle.
    void DeclareTonemap(rendergraph::RenderGraph& graph, rendergraph::RGHandle hdr, rendergraph::RGHandle bloom, rendergraph::RGHandle ldr,
                        bool clearColor, const rhi::ClearColor& clear, rhi::TextureFormat ldrFormat,
                        i32 vpX, i32 vpY, u32 vpW, u32 vpH, u32 frameIndex, u32 viewIndex, f32 exposure = 1.0f, f32 bloomIntensity = 0.0f,
                        Vec2 uvScale = Vec2{ 1, 1 }, Vec2 uvOffset = Vec2{ 0, 0 }) {
        rhi::RenderPipeline* pipeline = EnsurePipeline(ldrFormat);
        if (pipeline == nullptr) { return; }
        const u32 slot = (viewIndex % kMaxViews) * m_framesInFlight + (frameIndex % m_framesInFlight);
        const f32 push[6] = { exposure, bloomIntensity, uvScale.x, uvScale.y, uvOffset.x, uvOffset.y };

        const rhi::LoadOp load = clearColor ? rhi::LoadOp::Clear : rhi::LoadOp::Load;
        graph.AddRenderPass(u8"tonemap",
            [this, &graph, hdr, bloom, ldr, load, clear, vpX, vpY, vpW, vpH, pipeline, slot, push](rendergraph::PassBuilder& b) {
                b.SetColorTarget(0, ldr, load, rhi::StoreOp::Store, clear);
                b.ReadTexture(hdr);
                b.ReadTexture(bloom);
                b.SetViewport(vpX, vpY, vpW, vpH);
                b.NeverCull();
                b.SetExecute([this, &graph, hdr, bloom, pipeline, slot, push](rhi::RenderPassEncoder& rp) {
                    rhi::BindGroup* bg = EnsureBindGroup(slot, graph.GetTextureView(hdr), graph.GetTextureView(bloom),
                                                         graph.GetTextureGeneration(hdr) ^ (graph.GetTextureGeneration(bloom) * 1099511628211ull));
                    if (bg == nullptr) { return; }
                    rp.SetPipeline(pipeline);
                    rp.SetBindGroup(0, bg, Span<const u32>{});
                    rp.SetPushConstants(rhi::ShaderStage::Fragment, 0, sizeof(push), push);
                    rp.Draw(3, 1, 0, 0);
                });
            });
    }

private:
    static constexpr rhi::TextureFormat kHdrFormat = rhi::TextureFormat::RGBA16Float;
    static constexpr u32 kMaxFramesInFlight = 8;
    static constexpr u32 kMaxViews = 8;
    static constexpr u32 kMaxSlots = kMaxViews * kMaxFramesInFlight;

    // Build the fullscreen pipeline for `fmt` (rebuilt if the LDR target format changes — usually one).
    rhi::RenderPipeline* EnsurePipeline(rhi::TextureFormat fmt) {
        if (m_pipeline != nullptr && m_pipelineFormat == fmt) { return m_pipeline; }
        rhi::ShaderModule* vs = m_shaders->GetVariant(u8"tonemap", shaders::ShaderStage::Vertex,   shaders::ShaderFlags::None);
        rhi::ShaderModule* ps = m_shaders->GetVariant(u8"tonemap", shaders::ShaderStage::Fragment, shaders::ShaderFlags::None);
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
        pd.label = u8"tonemap";
        if (!m_device->CreateRenderPipeline(pd, m_pipeline).IsOk()) { m_pipeline = nullptr; return nullptr; }
        m_pipelineFormat = fmt;
        return m_pipeline;
    }

    // One bind group per (view, frame) slot over its HDR transient view. Rebuilt when the transient's
    // GENERATION changes (the graph stamps a fresh id whenever a different physical texture backs the
    // transient — e.g. on resize). Pointer identity alone is unsafe: a freed view address can be reused
    // by the new allocation, leaving the cached bind group pointing at a destroyed texture.
    rhi::BindGroup* EnsureBindGroup(u32 slot, rhi::TextureView* hdrView, rhi::TextureView* bloomView, u64 generation) {
        if (slot >= kMaxSlots || hdrView == nullptr || bloomView == nullptr) { return nullptr; }
        if (m_bindGroups[slot] != nullptr && m_bgViews[slot] == hdrView && m_bgBloom[slot] == bloomView && m_bgGen[slot] == generation) {
            return m_bindGroups[slot];
        }
        if (m_bindGroups[slot] != nullptr) { m_device->DestroyBindGroup(m_bindGroups[slot]); m_bindGroups[slot] = nullptr; }
        rhi::BindGroupEntry entries[] = {
            rhi::BindGroupEntry::TextureEntry(hdrView),
            rhi::BindGroupEntry::TextureEntry(bloomView),
            rhi::BindGroupEntry::SamplerEntry(m_sampler),
        };
        rhi::BindGroupDesc bgd{};
        bgd.layout = m_layout;
        bgd.entries = Span<const rhi::BindGroupEntry>{ entries, 3 };
        if (!m_device->CreateBindGroup(bgd, m_bindGroups[slot]).IsOk()) { m_bindGroups[slot] = nullptr; return nullptr; }
        m_bgViews[slot] = hdrView;
        m_bgBloom[slot] = bloomView;
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

    rhi::Sampler*          m_sampler = nullptr;       // linear-clamp, for the bloom composite
    rhi::BindGroup*        m_bindGroups[kMaxSlots] = {};
    rhi::TextureView*      m_bgViews[kMaxSlots] = {};
    rhi::TextureView*      m_bgBloom[kMaxSlots] = {};
    u64                    m_bgGen[kMaxSlots] = {};   // transient generation the cached BG was built for
};

} // namespace draconic::render
