/// Draconic::Render - the `:sky` partition.
///
/// Draws the environment as the visible background: a fullscreen triangle at the far plane, depth-
/// tested (LessEqual, no write) against the forward depth so it only fills pixels no geometry covered,
/// reconstructing a world-space view ray per pixel (inverse view-proj) and sampling the env cubemap.
/// Runs after the forward pass, into the same (HDR) color target, before tonemap - so the sky is in
/// the linear working space and gets tonemapped with the scene.

module;
#include "Core/Prelude.h"

export module draconic.render:sky;

import draconic.core;
import draconic.rhi;
import draconic.rendergraph;
import draconic.shaders;
import draconic.shaders.system;
import :data;   // kGVelocityFormat (sky writes camera-motion velocity for TAA)

using namespace draconic::core;
namespace rhi = draconic::rhi;

export namespace draconic::render {

// Fullscreen-triangle VS: emit far-plane NDC (z=1) + reconstruct the world-space ray via inverse
// view-proj (row-vector mul). PS samples the env cube along that ray.
// Sky uniform, shared by VS+PS. PrevViewProj (last frame, unjittered-equivalent via the Jitter unjitter)
// + Jitter let the sky write a camera-motion velocity so TAA reprojects the background under rotation.
inline constexpr const char8_t* kSkyCommon = u8R"(
cbuffer Sky : register(b0, space0) {
    row_major float4x4 InvViewProj;    // inverse of this frame's UNJITTERED view-proj (stable sky ray under TAA)
    row_major float4x4 PrevViewProj;   // last frame's view-proj (motion vectors)
    float4 CamPosIntensity;   // xyz = camera world pos, w = sky intensity
    float4 SunDir;            // xyz = light direction, w = sun angular size (deg)
    float4 SunColor;          // rgb = sun color, w = sun intensity
    float4 Jitter;            // xy = this frame's NDC jitter, zw = last frame's
};
)";

inline constexpr const char8_t* kSkyVS = u8R"(
struct VSOut { float4 pos : SV_Position; float3 dir : TEXCOORD0; float2 ndc : TEXCOORD1; };
VSOut main(uint vid : SV_VertexID) {
    float2 uv  = float2((vid << 1) & 2, vid & 2);
    float2 ndc = uv * 2.0 - 1.0;
    VSOut o;
    o.pos = float4(ndc, 1.0, 1.0);                        // far plane (depth = 1)
    o.ndc = ndc;
    // Reconstruct the world ray: at a given screen pixel the interpolated NDC matches what the scene's
    // geometry uses there (both go through the same viewport), so unproject the emitted NDC directly.
    float4 world = mul(float4(ndc, 1.0, 1.0), InvViewProj);  // clip -> world
    o.dir = world.xyz / world.w - CamPosIntensity.xyz;
    return o;
}
)";

inline constexpr const char8_t* kSkyPS = u8R"(
TextureCube  EnvMap  : register(t0, space0);
SamplerState EnvSamp : register(s0, space0);
struct PSIn { float4 pos : SV_Position; float3 dir : TEXCOORD0; float2 ndc : TEXCOORD1; };
struct PSOut { float4 color : SV_Target0; float2 velocity : SV_Target1; };
PSOut main(PSIn i) {
    float3 dir = normalize(i.dir);
    float3 c = EnvMap.SampleLevel(EnvSamp, dir, 0.0).rgb * CamPosIntensity.w;
    // Crisp analytic sun disc (screen resolution, round) toward the light, with a soft ~1.5deg edge.
    float3 L     = normalize(-SunDir.xyz);
    float  cd    = dot(dir, L);
    float  inner = cos(radians(max(SunDir.w, 0.1)));
    float  outer = cos(radians(max(SunDir.w, 0.1) + 1.5));
    c += smoothstep(outer, inner, cd) * SunColor.rgb * SunColor.w;

    // Camera-motion velocity: reproject the (infinite) view ray through last frame's view-proj (w=0, a
    // direction) and take the UV delta, in UNJITTERED NDC. The ray is reconstructed through the UNJITTERED
    // InvViewProj (so the background is temporally invariant under a static camera - no per-pixel jitter
    // oscillation for TAA to chase), which makes i.ndc the geometric current NDC directly. The previous
    // term still unjitters (PrevViewProj carries last frame's jitter; +Jitter.zw removes it).
    float4 prevClip = mul(float4(dir, 0.0), PrevViewProj);
    float2 curNDC   = i.ndc;
    float2 prevNDC  = prevClip.xy / prevClip.w + Jitter.zw;
    float2 velocity = (curNDC - prevNDC) * float2(0.5, -0.5);

    PSOut o; o.color = float4(c, 1.0); o.velocity = velocity; return o;
}
)";

class SkyPass {
public:
    SkyPass(rhi::Device& device, shaders::ShaderSystem& shaders, u32 framesInFlight) noexcept
        : m_device(&device), m_shaders(&shaders), m_fif(framesInFlight < 1 ? 1 : framesInFlight) {}
    ~SkyPass() { Shutdown(); }
    SkyPass(const SkyPass&) = delete;
    SkyPass& operator=(const SkyPass&) = delete;

    Status Initialize() {
        m_shaders->RegisterSource(u8"sky", shaders::ShaderStage::Vertex,   Concat(kSkyCommon, kSkyVS));
        m_shaders->RegisterSource(u8"sky", shaders::ShaderStage::Fragment, Concat(kSkyCommon, kSkyPS));
        rhi::BindGroupLayoutEntry uboE  = rhi::BindGroupLayoutEntry::UniformBuffer(0, rhi::ShaderStage::Vertex | rhi::ShaderStage::Fragment);
        rhi::BindGroupLayoutEntry texE  = rhi::BindGroupLayoutEntry::SampledTexture(0, rhi::ShaderStage::Fragment, rhi::TextureViewDimension::TextureCube);
        rhi::BindGroupLayoutEntry sampE = rhi::BindGroupLayoutEntry::Sampler(0, rhi::ShaderStage::Fragment);
        rhi::BindGroupLayoutEntry set0[] = { uboE, texE, sampE };
        rhi::BindGroupLayoutDesc ld{}; ld.entries = Span<const rhi::BindGroupLayoutEntry>{ set0, 3 };
        if (!m_device->CreateBindGroupLayout(ld, m_layout).IsOk()) { return Status{ ErrorCode::Unknown }; }
        rhi::BindGroupLayout* layouts[] = { m_layout };
        rhi::PipelineLayoutDesc pld{}; pld.bindGroupLayouts = Span<rhi::BindGroupLayout* const>{ layouts, 1 };
        if (!m_device->CreatePipelineLayout(pld, m_pipelineLayout).IsOk()) { return Status{ ErrorCode::Unknown }; }

        rhi::SamplerDesc ss{};
        ss.minFilter = rhi::FilterMode::Linear; ss.magFilter = rhi::FilterMode::Linear; ss.mipmapFilter = rhi::MipmapFilterMode::Linear;
        ss.addressU = rhi::AddressMode::ClampToEdge; ss.addressV = rhi::AddressMode::ClampToEdge; ss.addressW = rhi::AddressMode::ClampToEdge;
        ss.label = u8"sky.sampler";
        if (!m_device->CreateSampler(ss, m_sampler).IsOk()) { return Status{ ErrorCode::Unknown }; }
        return Status{};
    }

    // Declare the sky pass: load `color`, depth-test (read-only) against `depth`, read `envH`, draw a
    // fullscreen triangle sampling the env cube along the per-pixel world ray. `invViewProj` = inverse
    // of this view's view*proj; `camPos`/`intensity` scale the result.
    void DeclareSky(rendergraph::RenderGraph& graph, rendergraph::RGHandle color, rendergraph::RGHandle velocity,
                    rendergraph::RGHandle depth, rendergraph::RGHandle envH, rhi::TextureView* envView,
                    rhi::TextureFormat colorFormat, rhi::TextureFormat depthFormat,
                    const Float4x4& invViewProj, const Float4x4& prevViewProj, Float2 jitter, Float2 prevJitter,
                    const Float3& camPos, f32 intensity,
                    const Float3& sunDir, f32 sunSize, const Float3& sunColor, f32 sunIntensity,
                    i32 vpX, i32 vpY, u32 vpW, u32 vpH, u32 frameIndex, u32 viewIndex,
                    rendergraph::RGSubresourceRange colorSub = {}) {
        rhi::RenderPipeline* pipeline = EnsurePipeline(colorFormat, depthFormat);
        if (pipeline == nullptr || envView == nullptr) { return; }
        const u32 slot = (viewIndex % kMaxViews) * m_fif + (frameIndex % m_fif);
        SkyUniform u{};
        u.invViewProj = invViewProj;
        u.prevViewProj = prevViewProj;
        u.camPosIntensity = Float4{ camPos.x, camPos.y, camPos.z, intensity };
        u.sunDir   = Float4{ sunDir.x, sunDir.y, sunDir.z, sunSize };
        u.sunColor = Float4{ sunColor.x, sunColor.y, sunColor.z, sunIntensity };
        u.jitter   = Float4{ jitter.x, jitter.y, prevJitter.x, prevJitter.y };

        graph.AddRenderPass(u8"sky",
            [this, color, velocity, depth, envH, envView, pipeline, slot, u, vpX, vpY, vpW, vpH, colorSub](rendergraph::PassBuilder& b) {
                b.SetColorTarget(0, color, rhi::LoadOp::Load, rhi::StoreOp::Store, rhi::ClearColor::Black(), colorSub);
                b.SetColorTarget(1, velocity, rhi::LoadOp::Load, rhi::StoreOp::Store);   // camera-motion velocity for TAA
                b.SetReadOnlyDepthTarget(depth);     // depth test on, no write
                b.ReadTexture(envH);                 // order precompute -> sky + barrier readable
                b.SetViewport(vpX, vpY, vpW, vpH);
                b.NeverCull();
                b.SetExecute([this, envView, pipeline, slot, u](rhi::RenderPassEncoder& rp) {
                    rhi::BindGroup* bg = EnsureBindGroup(slot, envView, u);
                    if (bg == nullptr) { return; }
                    rp.SetPipeline(pipeline);
                    rp.SetBindGroup(0, bg, Span<const u32>{});
                    rp.Draw(3, 1, 0, 0);
                });
            });
    }

private:
    static constexpr u32 kMaxFIF = 4;
    static constexpr u32 kMaxViews = 8;
    static constexpr u32 kMaxSlots = kMaxViews * kMaxFIF;
    struct SkyUniform { Float4x4 invViewProj; Float4x4 prevViewProj; Float4 camPosIntensity; Float4 sunDir; Float4 sunColor; Float4 jitter; };

    static String Concat(const char8_t* a, const char8_t* b) { String s(StringView{ a }); s.Append(StringView{ b }); return s; }

    rhi::RenderPipeline* EnsurePipeline(rhi::TextureFormat colorFmt, rhi::TextureFormat depthFmt) {
        if (m_pipeline != nullptr && m_colorFormat == colorFmt && m_depthFormat == depthFmt) { return m_pipeline; }
        rhi::ShaderModule* vs = m_shaders->GetVariant(u8"sky", shaders::ShaderStage::Vertex,   shaders::ShaderFlags::None);
        rhi::ShaderModule* ps = m_shaders->GetVariant(u8"sky", shaders::ShaderStage::Fragment, shaders::ShaderFlags::None);
        if (vs == nullptr || ps == nullptr) { return nullptr; }
        if (m_pipeline != nullptr) { m_device->DestroyRenderPipeline(m_pipeline); m_pipeline = nullptr; }

        // 2 targets: color (matches the HDR target) + velocity (camera-motion, for TAA). No blend.
        rhi::ColorTargetState targets[2] = {};
        targets[0].format = colorFmt;
        targets[1].format = kGVelocityFormat;
        rhi::FragmentState frag{}; frag.shader = rhi::ProgrammableStage{ ps, u8"main", rhi::ShaderStage::Fragment };
        frag.targets = Span<const rhi::ColorTargetState>{ targets, 2 };
        rhi::DepthStencilState ds{}; ds.format = depthFmt; ds.depthTestEnabled = true; ds.depthWriteEnabled = false;
        ds.depthCompare = rhi::CompareFunction::LessEqual;   // pass at the far plane (background only)

        rhi::RenderPipelineDesc pd{};
        pd.layout = m_pipelineLayout;
        pd.vertex.shader = rhi::ProgrammableStage{ vs, u8"main", rhi::ShaderStage::Vertex };
        pd.fragment = frag;
        pd.depthStencil = ds;
        pd.primitive.topology = rhi::PrimitiveTopology::TriangleList;
        pd.primitive.cullMode = rhi::CullMode::None;
        pd.label = u8"sky";
        if (!m_device->CreateRenderPipeline(pd, m_pipeline).IsOk()) { m_pipeline = nullptr; return nullptr; }
        m_colorFormat = colorFmt; m_depthFormat = depthFmt;
        return m_pipeline;
    }

    rhi::BindGroup* EnsureBindGroup(u32 slot, rhi::TextureView* envView, const SkyUniform& u) {
        if (slot >= kMaxSlots) { return nullptr; }
        Slot& s = m_slots[slot];
        if (s.ubo == nullptr) {
            rhi::BufferDesc bd{}; bd.size = sizeof(SkyUniform); bd.usage = rhi::BufferUsage::Uniform; bd.memory = rhi::MemoryLocation::CpuToGpu; bd.label = u8"sky.ubo";
            if (!m_device->CreateBuffer(bd, s.ubo).IsOk()) { s.ubo = nullptr; return nullptr; }
        }
        if (void* p = s.ubo->Map()) { MemCopy(p, &u, sizeof(SkyUniform)); s.ubo->Unmap(); }
        if (s.bindGroup == nullptr || s.env != envView) {
            if (s.bindGroup != nullptr) { m_device->DestroyBindGroup(s.bindGroup); s.bindGroup = nullptr; }
            rhi::BindGroupEntry be[] = {
                rhi::BindGroupEntry::BufferEntry(s.ubo, 0, sizeof(SkyUniform)),
                rhi::BindGroupEntry::TextureEntry(envView),
                rhi::BindGroupEntry::SamplerEntry(m_sampler),
            };
            rhi::BindGroupDesc bgd{}; bgd.layout = m_layout; bgd.entries = Span<const rhi::BindGroupEntry>{ be, 3 };
            if (!m_device->CreateBindGroup(bgd, s.bindGroup).IsOk()) { s.bindGroup = nullptr; return nullptr; }
            s.env = envView;
        }
        return s.bindGroup;
    }

    void Shutdown() {
        for (u32 i = 0; i < kMaxSlots; ++i) {
            if (m_slots[i].bindGroup) { m_device->DestroyBindGroup(m_slots[i].bindGroup); m_slots[i].bindGroup = nullptr; }
            if (m_slots[i].ubo) { m_device->DestroyBuffer(m_slots[i].ubo); m_slots[i].ubo = nullptr; }
        }
        if (m_pipeline) { m_device->DestroyRenderPipeline(m_pipeline); m_pipeline = nullptr; }
        if (m_sampler) { m_device->DestroySampler(m_sampler); m_sampler = nullptr; }
        if (m_pipelineLayout) { m_device->DestroyPipelineLayout(m_pipelineLayout); m_pipelineLayout = nullptr; }
        if (m_layout) { m_device->DestroyBindGroupLayout(m_layout); m_layout = nullptr; }
    }

    struct Slot { rhi::Buffer* ubo = nullptr; rhi::BindGroup* bindGroup = nullptr; rhi::TextureView* env = nullptr; };

    rhi::Device*           m_device;
    shaders::ShaderSystem* m_shaders;
    u32                    m_fif = 2;
    rhi::BindGroupLayout*  m_layout = nullptr;
    rhi::PipelineLayout*   m_pipelineLayout = nullptr;
    rhi::RenderPipeline*   m_pipeline = nullptr;
    rhi::TextureFormat     m_colorFormat = rhi::TextureFormat::Undefined;
    rhi::TextureFormat     m_depthFormat = rhi::TextureFormat::Undefined;
    rhi::Sampler*          m_sampler = nullptr;
    Slot                   m_slots[kMaxSlots] = {};
};

} // namespace draconic::render
