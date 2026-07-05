/// Draconic::Render — the `:bloom` partition.
///
/// HDR bloom via a downsample/upsample pyramid (Jimenez "Next Generation Post Processing in Call of
/// Duty" — 13-tap downsample + 9-tap tent upsample, additive). The first downsample soft-knee-
/// thresholds + Karis-averages to keep fireflies out. The pyramid is built from render-graph
/// transients (sized per view, so no resize handling); the result is composited additively by the
/// tonemap pass. Runs on the linear HDR scene, before tonemap.

module;
#include "Core/Prelude.h"

export module draconic.render:bloom;

import draconic.core;
import draconic.rhi;
import draconic.rendergraph;
import draconic.shaders;
import draconic.shaders.system;

using namespace draconic::core;
namespace rhi = draconic::rhi;

export namespace draconic::render {

// Fullscreen-triangle VS with a top-origin [0,1] uv (uv.y=0 at the top). The RHI's negative-viewport
// Y-flip means a naive uv would run bottom-up, so every RT-sampling pass would flip Y — flip uv.y here
// once so the whole pyramid (and the tonemap composite) stays orientation-consistent with the RTs.
inline constexpr const char8_t* kBloomVS = u8R"(
struct VSOut { float4 pos : SV_Position; float2 uv : TEXCOORD0; };
VSOut main(uint vid : SV_VertexID) {
    VSOut o;
    float2 raw = float2((vid << 1) & 2, vid & 2);
    o.pos = float4(raw * 2.0 - 1.0, 0.0, 1.0);
    o.uv  = float2(raw.x, 1.0 - raw.y);
    return o;
}
)";

inline constexpr const char8_t* kBloomCommon = u8R"(
Texture2D<float4> Src  : register(t0, space0);
SamplerState      Samp : register(s0, space0);
struct BloomPush {
    float2 SrcTexel;   // 1 / source size (filter tap spacing)
    float  Threshold;  // brightness cutoff (first downsample only)
    float  Knee;       // soft-knee width
    int    FirstPass;  // 1 = threshold + firefly-average the source (mip 0)
    float3 _pad;
};
[[vk::push_constant]] BloomPush pc;
)";

// 13-tap downsample (CoD/Jimenez). On the first pass, soft-knee threshold + a Karis luma weighting on
// the 2x2 groups to stop single bright pixels from causing bloom flicker.
inline constexpr const char8_t* kBloomDownPS = u8R"(
float3 Prefilter(float3 c) {
    float br   = max(c.r, max(c.g, c.b));
    float soft = clamp(br - pc.Threshold + pc.Knee, 0.0, 2.0 * pc.Knee);
    soft       = (soft * soft) / (4.0 * pc.Knee + 1e-5);
    float contrib = max(soft, br - pc.Threshold) / max(br, 1e-5);
    return c * contrib;
}
float KarisWeight(float3 c) { return 1.0 / (1.0 + max(c.r, max(c.g, c.b))); }
float4 main(float4 pos : SV_Position, float2 uv : TEXCOORD0) : SV_Target {
    float2 t = pc.SrcTexel;
    float3 a = Src.SampleLevel(Samp, uv + t * float2(-2,-2), 0).rgb;
    float3 b = Src.SampleLevel(Samp, uv + t * float2( 0,-2), 0).rgb;
    float3 c = Src.SampleLevel(Samp, uv + t * float2( 2,-2), 0).rgb;
    float3 d = Src.SampleLevel(Samp, uv + t * float2(-2, 0), 0).rgb;
    float3 e = Src.SampleLevel(Samp, uv,                     0).rgb;
    float3 f = Src.SampleLevel(Samp, uv + t * float2( 2, 0), 0).rgb;
    float3 g = Src.SampleLevel(Samp, uv + t * float2(-2, 2), 0).rgb;
    float3 h = Src.SampleLevel(Samp, uv + t * float2( 0, 2), 0).rgb;
    float3 i = Src.SampleLevel(Samp, uv + t * float2( 2, 2), 0).rgb;
    float3 j = Src.SampleLevel(Samp, uv + t * float2(-1,-1), 0).rgb;
    float3 k = Src.SampleLevel(Samp, uv + t * float2( 1,-1), 0).rgb;
    float3 l = Src.SampleLevel(Samp, uv + t * float2(-1, 1), 0).rgb;
    float3 m = Src.SampleLevel(Samp, uv + t * float2( 1, 1), 0).rgb;
    float3 result;
    if (pc.FirstPass != 0) {
        // Karis-weighted average of the 5 inner 2x2 groups (firefly suppression), then threshold.
        float3 g0 = (j + k + l + m) * 0.25;
        float3 g1 = (a + b + d + e) * 0.25;
        float3 g2 = (b + c + e + f) * 0.25;
        float3 g3 = (d + e + g + h) * 0.25;
        float3 g4 = (e + f + h + i) * 0.25;
        float w0 = KarisWeight(g0), w1 = KarisWeight(g1), w2 = KarisWeight(g2), w3 = KarisWeight(g3), w4 = KarisWeight(g4);
        result = (g0*w0*0.5 + g1*w1*0.125 + g2*w2*0.125 + g3*w3*0.125 + g4*w4*0.125)
               / max(w0*0.5 + w1*0.125 + w2*0.125 + w3*0.125 + w4*0.125, 1e-5);
        result = Prefilter(result);
    } else {
        result = e * 0.125
               + (a + c + g + i) * 0.03125
               + (b + d + f + h) * 0.0625
               + (j + k + l + m) * 0.125;
    }
    return float4(result, 1.0);
}
)";

// 9-tap tent upsample; the pipeline uses additive blend so it accumulates onto the finer mip.
inline constexpr const char8_t* kBloomUpPS = u8R"(
float4 main(float4 pos : SV_Position, float2 uv : TEXCOORD0) : SV_Target {
    float2 t = pc.SrcTexel;
    float3 s = Src.SampleLevel(Samp, uv + t * float2(-1,-1), 0).rgb * 1.0
             + Src.SampleLevel(Samp, uv + t * float2( 0,-1), 0).rgb * 2.0
             + Src.SampleLevel(Samp, uv + t * float2( 1,-1), 0).rgb * 1.0
             + Src.SampleLevel(Samp, uv + t * float2(-1, 0), 0).rgb * 2.0
             + Src.SampleLevel(Samp, uv,                     0).rgb * 4.0
             + Src.SampleLevel(Samp, uv + t * float2( 1, 0), 0).rgb * 2.0
             + Src.SampleLevel(Samp, uv + t * float2(-1, 1), 0).rgb * 1.0
             + Src.SampleLevel(Samp, uv + t * float2( 0, 1), 0).rgb * 2.0
             + Src.SampleLevel(Samp, uv + t * float2( 1, 1), 0).rgb * 1.0;
    return float4(s * (1.0 / 16.0), 1.0);
}
)";

// Builds the bloom pyramid from an HDR input; returns the mip-0 (half-res) accumulated bloom handle,
// which the tonemap composites. Owns the down/up pipelines + sampler; the pyramid is graph transients.
class BloomPass {
public:
    static constexpr rhi::TextureFormat kBloomFormat = rhi::TextureFormat::RGBA16Float;
    static constexpr u32 kMaxLevels = 7;

    BloomPass(rhi::Device& device, shaders::ShaderSystem& shaders) noexcept
        : m_device(&device), m_shaders(&shaders) {}
    ~BloomPass() { Shutdown(); }
    BloomPass(const BloomPass&) = delete;
    BloomPass& operator=(const BloomPass&) = delete;

    Status Initialize() {
        m_shaders->RegisterSource(u8"bloom_ds", shaders::ShaderStage::Vertex,   kBloomVS);
        m_shaders->RegisterSource(u8"bloom_ds", shaders::ShaderStage::Fragment, Concat(kBloomCommon, kBloomDownPS));
        m_shaders->RegisterSource(u8"bloom_us", shaders::ShaderStage::Vertex,   kBloomVS);
        m_shaders->RegisterSource(u8"bloom_us", shaders::ShaderStage::Fragment, Concat(kBloomCommon, kBloomUpPS));

        rhi::BindGroupLayoutEntry tex  = rhi::BindGroupLayoutEntry::SampledTexture(0, rhi::ShaderStage::Fragment);
        rhi::BindGroupLayoutEntry samp = rhi::BindGroupLayoutEntry::Sampler(0, rhi::ShaderStage::Fragment);
        rhi::BindGroupLayoutEntry e[] = { tex, samp };
        rhi::BindGroupLayoutDesc ld{}; ld.entries = Span<const rhi::BindGroupLayoutEntry>{ e, 2 };
        if (!m_device->CreateBindGroupLayout(ld, m_layout).IsOk()) { return Status{ ErrorCode::Unknown }; }

        rhi::BindGroupLayout* layouts[] = { m_layout };
        rhi::PushConstantRange pc{}; pc.stages = rhi::ShaderStage::Fragment; pc.offset = 0; pc.size = sizeof(BloomPush);
        rhi::PipelineLayoutDesc pld{};
        pld.bindGroupLayouts = Span<rhi::BindGroupLayout* const>{ layouts, 1 };
        pld.pushConstantRanges = Span<const rhi::PushConstantRange>{ &pc, 1 };
        if (!m_device->CreatePipelineLayout(pld, m_pipelineLayout).IsOk()) { return Status{ ErrorCode::Unknown }; }

        rhi::SamplerDesc ss{};
        ss.minFilter = rhi::FilterMode::Linear; ss.magFilter = rhi::FilterMode::Linear;
        ss.addressU = rhi::AddressMode::ClampToEdge; ss.addressV = rhi::AddressMode::ClampToEdge; ss.addressW = rhi::AddressMode::ClampToEdge;
        ss.label = u8"bloom.sampler";
        if (!m_device->CreateSampler(ss, m_sampler).IsOk()) { return Status{ ErrorCode::Unknown }; }

        m_downPipeline = MakePipeline(u8"bloom_ds", /*additive*/ false);
        m_upPipeline   = MakePipeline(u8"bloom_us", /*additive*/ true);
        if (m_downPipeline == nullptr || m_upPipeline == nullptr) { return Status{ ErrorCode::Unknown }; }
        return Status{};
    }

    [[nodiscard]] rhi::TextureFormat Format() const noexcept { return kBloomFormat; }

    // Declare the pyramid for one view's HDR input. Returns the accumulated bloom (mip 0, half-res), or
    // an invalid handle if the view is too small. threshold/knee gate what blooms (soft-knee prefilter).
    [[nodiscard]] rendergraph::RGHandle DeclareBloom(rendergraph::RenderGraph& graph, rendergraph::RGHandle hdr,
                                                     u32 vpW, u32 vpH, f32 threshold, f32 knee) {
        if (vpW < 4 || vpH < 4) { return {}; }
        // Level count: halve until a small floor, capped.
        u32 levels = 1;
        for (u32 w = vpW / 2, h = vpH / 2; levels < kMaxLevels && w > 8 && h > 8; w /= 2, h /= 2) { ++levels; }

        rendergraph::RGHandle chain[kMaxLevels] = {};
        u32 lw[kMaxLevels] = {}, lh[kMaxLevels] = {};
        u32 w = vpW, h = vpH;
        for (u32 i = 0; i < levels; ++i) {
            w = Max(1u, w / 2); h = Max(1u, h / 2);
            lw[i] = w; lh[i] = h;
            chain[i] = graph.CreateTransient(u8"bloom.mip", rendergraph::RGTextureDesc(kBloomFormat, w, h));
        }

        // Downsample: hdr -> mip0 (threshold), mip(i-1) -> mip(i).
        for (u32 i = 0; i < levels; ++i) {
            const rendergraph::RGHandle src = (i == 0) ? hdr : chain[i - 1];
            const u32 srcW = (i == 0) ? vpW : lw[i - 1];
            const u32 srcH = (i == 0) ? vpH : lh[i - 1];
            BloomPush push{};
            push.srcTexel = Vector2{ 1.0f / static_cast<f32>(srcW), 1.0f / static_cast<f32>(srcH) };
            push.threshold = threshold; push.knee = knee; push.firstPass = (i == 0) ? 1 : 0;
            const rendergraph::RGHandle dst = chain[i];
            const u32 dw = lw[i], dh = lh[i];
            graph.AddRenderPass(u8"bloom.down", [this, &graph, src, dst, dw, dh, push](rendergraph::PassBuilder& b) {
                b.SetColorTarget(0, dst, rhi::LoadOp::Clear, rhi::StoreOp::Store, rhi::ClearColor::Black());
                b.ReadTexture(src);
                b.SetViewport(0, 0, dw, dh);
                b.NeverCull();
                b.SetExecute([this, &graph, src, push](rhi::RenderPassEncoder& rp) {
                    rhi::BindGroup* bg = EnsureBindGroup(graph.GetTextureView(src), graph.GetTextureGeneration(src));
                    if (bg == nullptr) { return; }
                    rp.SetPipeline(m_downPipeline);
                    rp.SetBindGroup(0, bg, Span<const u32>{});
                    rp.SetPushConstants(rhi::ShaderStage::Fragment, 0, sizeof(BloomPush), &push);
                    rp.Draw(3, 1, 0, 0);
                });
            });
        }

        // Upsample (additive): mip(i+1) -> add into mip(i), coarsest to finest.
        for (i32 i = static_cast<i32>(levels) - 2; i >= 0; --i) {
            const rendergraph::RGHandle src = chain[i + 1];
            const rendergraph::RGHandle dst = chain[i];
            BloomPush push{};
            push.srcTexel = Vector2{ 1.0f / static_cast<f32>(lw[i + 1]), 1.0f / static_cast<f32>(lh[i + 1]) };
            const u32 dw = lw[i], dh = lh[i];
            graph.AddRenderPass(u8"bloom.up", [this, &graph, src, dst, dw, dh, push](rendergraph::PassBuilder& b) {
                b.SetColorTarget(0, dst, rhi::LoadOp::Load, rhi::StoreOp::Store, rhi::ClearColor::Black());   // additive blend
                b.ReadTexture(src);
                b.SetViewport(0, 0, dw, dh);
                b.NeverCull();
                b.SetExecute([this, &graph, src, push](rhi::RenderPassEncoder& rp) {
                    rhi::BindGroup* bg = EnsureBindGroup(graph.GetTextureView(src), graph.GetTextureGeneration(src));
                    if (bg == nullptr) { return; }
                    rp.SetPipeline(m_upPipeline);
                    rp.SetBindGroup(0, bg, Span<const u32>{});
                    rp.SetPushConstants(rhi::ShaderStage::Fragment, 0, sizeof(BloomPush), &push);
                    rp.Draw(3, 1, 0, 0);
                });
            });
        }
        return chain[0];
    }

private:
    struct BloomPush { Vector2 srcTexel{}; f32 threshold = 1.0f; f32 knee = 0.5f; i32 firstPass = 0; f32 pad0 = 0, pad1 = 0, pad2 = 0; };

    static String Concat(const char8_t* a, const char8_t* b) { String s(StringView{ a }); s.Append(StringView{ b }); return s; }

    rhi::RenderPipeline* MakePipeline(StringView name, bool additive) {
        rhi::ShaderModule* vs = m_shaders->GetVariant(name, shaders::ShaderStage::Vertex,   shaders::ShaderFlags::None);
        rhi::ShaderModule* ps = m_shaders->GetVariant(name, shaders::ShaderStage::Fragment, shaders::ShaderFlags::None);
        if (vs == nullptr || ps == nullptr) { return nullptr; }
        rhi::ColorTargetState color{}; color.format = kBloomFormat;
        if (additive) { color.blend = rhi::BlendState::Additive(); }
        rhi::FragmentState frag{}; frag.shader = rhi::ProgrammableStage{ ps, u8"main", rhi::ShaderStage::Fragment };
        frag.targets = Span<const rhi::ColorTargetState>{ &color, 1 };
        rhi::RenderPipelineDesc pd{};
        pd.layout = m_pipelineLayout;
        pd.vertex.shader = rhi::ProgrammableStage{ vs, u8"main", rhi::ShaderStage::Vertex };
        pd.fragment = frag;
        pd.primitive.topology = rhi::PrimitiveTopology::TriangleList;
        pd.primitive.cullMode = rhi::CullMode::None;
        pd.label = name;
        rhi::RenderPipeline* p = nullptr;
        if (!m_device->CreateRenderPipeline(pd, p).IsOk()) { return nullptr; }
        return p;
    }

    // Bind group over a (transient) source view, cached by view pointer + generation. Transients are
    // pooled with stable generations across frames, so this stabilizes; a resize bumps the generation.
    rhi::BindGroup* EnsureBindGroup(rhi::TextureView* view, u64 generation) {
        if (view == nullptr) { return nullptr; }
        if (Entry* e = m_bindGroups.Find(view)) {
            if (e->gen == generation && e->bg != nullptr) { return e->bg; }
            if (e->bg != nullptr) { m_device->DestroyBindGroup(e->bg); e->bg = nullptr; }
        }
        rhi::BindGroupEntry ent[] = { rhi::BindGroupEntry::TextureEntry(view), rhi::BindGroupEntry::SamplerEntry(m_sampler) };
        rhi::BindGroupDesc bgd{}; bgd.layout = m_layout; bgd.entries = Span<const rhi::BindGroupEntry>{ ent, 2 };
        rhi::BindGroup* bg = nullptr;
        if (!m_device->CreateBindGroup(bgd, bg).IsOk()) { return nullptr; }
        m_bindGroups.InsertOrAssign(view, Entry{ bg, generation });
        return bg;
    }

    void Shutdown() {
        for (auto& kv : m_bindGroups) { if (kv.value.bg != nullptr) { m_device->DestroyBindGroup(kv.value.bg); } }
        m_bindGroups.Clear();
        if (m_downPipeline != nullptr) { m_device->DestroyRenderPipeline(m_downPipeline); m_downPipeline = nullptr; }
        if (m_upPipeline != nullptr) { m_device->DestroyRenderPipeline(m_upPipeline); m_upPipeline = nullptr; }
        if (m_sampler != nullptr) { m_device->DestroySampler(m_sampler); m_sampler = nullptr; }
        if (m_pipelineLayout != nullptr) { m_device->DestroyPipelineLayout(m_pipelineLayout); m_pipelineLayout = nullptr; }
        if (m_layout != nullptr) { m_device->DestroyBindGroupLayout(m_layout); m_layout = nullptr; }
    }

    struct Entry { rhi::BindGroup* bg = nullptr; u64 gen = 0; };

    rhi::Device*           m_device;
    shaders::ShaderSystem* m_shaders;
    rhi::BindGroupLayout*  m_layout = nullptr;
    rhi::PipelineLayout*   m_pipelineLayout = nullptr;
    rhi::RenderPipeline*   m_downPipeline = nullptr;
    rhi::RenderPipeline*   m_upPipeline = nullptr;
    rhi::Sampler*          m_sampler = nullptr;
    HashMap<rhi::TextureView*, Entry> m_bindGroups;
};

} // namespace draconic::render
