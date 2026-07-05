/// Draconic::Render — the `:decal_pass` partition.
///
/// Screen-space projected decals, ported from SedulousEngine's DecalPass/decal.frag: reconstruct the
/// world position under each pixel from the scene depth, transform it into a decal's oriented unit box,
/// clip to the box, and alpha-blend the decal texture onto the lit HDR — a "sprayed" sticker that lands
/// on whatever surface is under the box (including animated meshes, since it reads the depth buffer).
///
/// Runs after the forward+sky pass and BEFORE AO/TAA, blending into the HDR (so decals get TAA-resolved).
/// Draconic matches Sedulous's shader assumptions (row-major, D3D-style [0,1] clip depth, top-origin uv
/// under the negative viewport — same reconstruction as AoPass), so the projection math ports verbatim.
///
/// v1 draws a FULLSCREEN triangle per decal (robust across split-screen sub-rects + no box winding/cull
/// pitfalls); the box test lives in the fragment shader. Receiver normal for angle-fade comes from the
/// reconstructed world-position derivatives (ddx/ddy), faithful to the port. (A per-decal box mesh +
/// sampling the normalT G-buffer are later optimizations.)

module;
#include "Core/Prelude.h"

export module draconic.render:decal_pass;

import draconic.core;
import draconic.rhi;
import draconic.rendergraph;
import draconic.shaders;
import draconic.shaders.system;
import :data;
import :resources;

using namespace draconic::core;
namespace rhi = draconic::rhi;

export namespace draconic::render {

// The scene HDR format decals blend into (matches TonemapPass::HdrFormat / AoPass).
inline constexpr rhi::TextureFormat kDecalHdrFormat = rhi::TextureFormat::RGBA16Float;

// One decal, uploaded to the per-decal UBO (set 1, dynamic offset). 224 bytes.
struct DecalUniforms {
    Matrix4 world;         // decal box world transform (scale = box size); projects along local +Z
    Matrix4 invWorld;      // world -> decal-local (the box clip)
    Matrix4 invViewProj;   // (ndc, depth) -> world, for depth reconstruction (this view, jittered)
    Vector4 color;         // rgba tint
    Vector4 params;        // x,y = 1/full-target-size ; z,w = cos(angleFadeStart), cos(angleFadeEnd)
};
static_assert(sizeof(DecalUniforms) == 224);

// Fullscreen triangle that EMITS its clip-space NDC (like SkyPass): the interpolated NDC at a pixel is
// exactly what the scene geometry used there (same viewport), so unprojecting it is robust — no
// hand-derived negative-viewport flip. SV_Position is still used to sample depth at the right texel.
inline constexpr const char8_t* kDecalVS = u8R"(
struct VSOut { float4 pos : SV_Position; float2 ndc : TEXCOORD0; };
VSOut main(uint vid : SV_VertexID) {
    float2 ndc = float2((vid << 1) & 2, vid & 2) * 2.0 - 1.0;
    VSOut o;
    o.pos = float4(ndc, 0.0, 1.0);
    o.ndc = ndc;
    return o;
}
)";

inline constexpr const char8_t* kDecalPS = u8R"(
#pragma pack_matrix(row_major)
Texture2D    SceneDepth : register(t0, space0);
SamplerState DepthSamp  : register(s0, space0);
Texture2D    DecalTex   : register(t0, space2);
SamplerState DecalSamp  : register(s0, space2);
cbuffer DecalUniforms : register(b0, space1) {
    row_major float4x4 World;
    row_major float4x4 InvWorld;
    row_major float4x4 InvViewProj;
    float4   Color;
    float4   Params;      // xy = 1/full-size, z = cos(fadeStart), w = cos(fadeEnd)
};

float4 main(float4 pos : SV_Position, float2 ndc : TEXCOORD0) : SV_Target {
    // Sample the scene depth at THIS framebuffer pixel (SV_Position is the framebuffer position, so
    // pixel/size reads the texel the forward pass wrote here — no flip needed for a same-pixel read).
    float2 uv    = pos.xy * Params.xy;
    float  depth = SceneDepth.SampleLevel(DepthSamp, uv, 0).r;

    // Reconstruct world position from the EMITTED clip NDC (the true clip coord this pixel's geometry
    // used) + the sampled depth. clip -> world via InvViewProj (same as SkyPass).
    float4 h        = mul(float4(ndc, depth, 1.0), InvViewProj);
    float3 worldPos = h.xyz / h.w;

    // Into the decal's unit box; clip outside [-0.5, 0.5].
    float3 local = mul(float4(worldPos, 1.0), InvWorld).xyz;
    if (any(abs(local) > 0.5)) { discard; }

    // Decal UV from the box's local XY (project along local Z). Flip V for top-origin texture space.
    float2 decalUV = float2(local.x + 0.5, 0.5 - local.y);

    // Angle fade: receiver normal from world-pos screen derivatives vs the decal's projection axis
    // (local +Z in world = World row 2). Fade out where the surface faces away from the projection.
    float3 N        = normalize(cross(ddy(worldPos), ddx(worldPos)));
    float3 decalFwd = normalize(float3(World._m20, World._m21, World._m22));
    float  cosA     = dot(N, -decalFwd);
    float  fade     = smoothstep(Params.w, Params.z, cosA);

    float4 c = DecalTex.Sample(DecalSamp, decalUV) * Color;
    c.a *= fade;
    return c;
}
)";

class DecalPass {
public:
    DecalPass(rhi::Device& device, shaders::ShaderSystem& shaders, u32 framesInFlight) noexcept
        : m_device(&device), m_shaders(&shaders),
          m_decalRing(device, framesInFlight, sizeof(DecalUniforms) <= 256 ? 256u : 512u,
                      rhi::BufferUsage::Uniform | rhi::BufferUsage::CopyDst, u8"decal.uniforms") {}
    ~DecalPass() { Shutdown(); }
    DecalPass(const DecalPass&) = delete;
    DecalPass& operator=(const DecalPass&) = delete;

    Status Initialize() {
        m_shaders->RegisterSource(u8"decal", shaders::ShaderStage::Vertex,   kDecalVS);
        m_shaders->RegisterSource(u8"decal", shaders::ShaderStage::Fragment, kDecalPS);

        // set 0: scene depth (t0) + sampler (s0). set 1: per-decal UBO (dynamic). set 2: decal texture.
        rhi::BindGroupLayoutEntry depthEntries[] = {
            rhi::BindGroupLayoutEntry::SampledTexture(0, rhi::ShaderStage::Fragment),
            rhi::BindGroupLayoutEntry::Sampler(0, rhi::ShaderStage::Fragment),
        };
        rhi::BindGroupLayoutDesc dld{}; dld.entries = Span<const rhi::BindGroupLayoutEntry>{ depthEntries, 2 };
        if (!m_device->CreateBindGroupLayout(dld, m_depthLayout).IsOk()) { return Status{ ErrorCode::Unknown }; }

        rhi::BindGroupLayoutEntry uboEntry = rhi::BindGroupLayoutEntry::UniformBuffer(0, rhi::ShaderStage::Fragment);
        uboEntry.hasDynamicOffset = true;
        rhi::BindGroupLayoutDesc uld{}; uld.entries = Span<const rhi::BindGroupLayoutEntry>{ &uboEntry, 1 };
        if (!m_device->CreateBindGroupLayout(uld, m_uboLayout).IsOk()) { return Status{ ErrorCode::Unknown }; }

        rhi::BindGroupLayoutEntry texEntries[] = {
            rhi::BindGroupLayoutEntry::SampledTexture(0, rhi::ShaderStage::Fragment),
            rhi::BindGroupLayoutEntry::Sampler(0, rhi::ShaderStage::Fragment),
        };
        rhi::BindGroupLayoutDesc tld{}; tld.entries = Span<const rhi::BindGroupLayoutEntry>{ texEntries, 2 };
        if (!m_device->CreateBindGroupLayout(tld, m_texLayout).IsOk()) { return Status{ ErrorCode::Unknown }; }

        rhi::BindGroupLayout* layouts[] = { m_depthLayout, m_uboLayout, m_texLayout };
        rhi::PipelineLayoutDesc pld{};
        pld.bindGroupLayouts = Span<rhi::BindGroupLayout* const>{ layouts, 3 };
        if (!m_device->CreatePipelineLayout(pld, m_pipelineLayout).IsOk()) { return Status{ ErrorCode::Unknown }; }

        auto mkSampler = [this](rhi::FilterMode f, rhi::Sampler*& out) {
            rhi::SamplerDesc s{}; s.minFilter = f; s.magFilter = f;
            s.addressU = rhi::AddressMode::ClampToEdge; s.addressV = rhi::AddressMode::ClampToEdge; s.addressW = rhi::AddressMode::ClampToEdge;
            return m_device->CreateSampler(s, out).IsOk();
        };
        if (!mkSampler(rhi::FilterMode::Nearest, m_depthSampler) || !mkSampler(rhi::FilterMode::Linear, m_texSampler)) {
            return Status{ ErrorCode::Unknown };
        }

        m_pipeline = MakePipeline();
        if (m_pipeline == nullptr) { return Status{ ErrorCode::Unknown }; }
        return Status{};
    }

    // Bracket the frame ONCE (before the per-view loop). The per-decal uniform ring must NOT be reset per
    // view — DeclareDecals is called once per view, and the GPU reads the ring at graph-execute time
    // (after ALL views have recorded), so per-view resets would make every view read the last view's
    // slot (its InvViewProj) -> decals reconstruct with the wrong matrix and swim / couple across views.
    void BeginFrame(u32 frameIndex) {
        Tick(frameIndex);
        if (!m_reserved) { m_reserved = m_decalRing.Reserve(kMaxDecalsPerFrame); }
        m_decalRing.BeginFrame(frameIndex);
    }
    void EndFrame() { m_decalRing.EndFrame(); }

    // Blend the scene's decals into `hdr` for one view. `viewProj` is the view's (jittered) view-proj,
    // whose inverse reconstructs world position from `depth`. Allocates its own ring slots (accumulating
    // across views within the frame). No-op if there are no decals. Bracket with BeginFrame/EndFrame.
    void DeclareDecals(rendergraph::RenderGraph& graph, rendergraph::RGHandle hdr, rendergraph::RGHandle depth,
                       Span<const DecalInstance> decals, const Matrix4& viewProj, u32 w, u32 h,
                       i32 vpX, i32 vpY, u32 vpW, u32 vpH) {
        if (decals.IsEmpty() || w == 0 || h == 0 || m_decalRing.Buffer() == nullptr) { return; }

        const Matrix4 invViewProj = Inverse(viewProj);
        const Vector2 invSize{ 1.0f / static_cast<f32>(w), 1.0f / static_cast<f32>(h) };

        // This view's decals -> fresh ring slots (its own InvViewProj). Local list, captured by value into
        // the pass so it survives to execute time (a shared member would be clobbered by the next view).
        Array<Draw> viewDraws;
        for (const DecalInstance& d : decals) {
            if (d.texture == nullptr) { continue; }
            const DynamicUniformRing::Range r = m_decalRing.Allocate();
            if (!r.ok) { break; }
            DecalUniforms u{};
            u.world       = d.world;
            u.invWorld    = Inverse(d.world);
            u.invViewProj = invViewProj;
            u.color       = Vector4{ d.color.r, d.color.g, d.color.b, d.color.a };
            u.params      = Vector4{ invSize.x, invSize.y, Cos(d.fadeStart), Cos(d.fadeEnd) };
            MemCopy(r.ptr, &u, sizeof(u));
            viewDraws.PushBack(Draw{ r.byteOffset, d.texture });
        }
        if (viewDraws.IsEmpty()) { return; }

        rhi::BindGroup* uboBg = EnsureUboBindGroup();
        graph.AddRenderPass(u8"decal", [this, &graph, hdr, depth, uboBg, vpX, vpY, vpW, vpH, draws = viewDraws](rendergraph::PassBuilder& b) {
            b.SetColorTarget(0, hdr, rhi::LoadOp::Load, rhi::StoreOp::Store, rhi::ClearColor::Black());
            b.ReadTexture(depth);
            // The view's sub-rect (matches the forward/sky), so the fullscreen tri's emitted NDC lines up
            // with the scene geometry's NDC at each pixel.
            b.SetViewport(vpX, vpY, vpW, vpH);
            b.NeverCull();
            b.SetExecute([this, &graph, depth, uboBg, draws](rhi::RenderPassEncoder& rp) {
                rhi::BindGroup* depthBg = EnsureDepthBindGroup(graph.GetTextureView(depth), graph.GetTextureGeneration(depth));
                if (depthBg == nullptr || uboBg == nullptr) { return; }
                rp.SetPipeline(m_pipeline);
                rp.SetBindGroup(0, depthBg, Span<const u32>{});
                for (const Draw& d : draws) {
                    rhi::BindGroup* texBg = EnsureTextureBindGroup(d.texture);
                    if (texBg == nullptr) { continue; }
                    rp.SetBindGroup(1, uboBg, Span<const u32>{ &d.offset, 1 });
                    rp.SetBindGroup(2, texBg, Span<const u32>{});
                    rp.Draw(3, 1, 0, 0);
                }
            });
        });
    }

private:
    struct Draw { u32 offset; rhi::TextureView* texture; };

    static constexpr u32 kRetireFrames      = 3;
    static constexpr u32 kMaxDecalsPerFrame = 256;   // ring capacity (across all views) — reserved once

    rhi::RenderPipeline* MakePipeline() {
        rhi::ShaderModule* vs = m_shaders->GetVariant(u8"decal", shaders::ShaderStage::Vertex,   shaders::ShaderFlags::None);
        rhi::ShaderModule* ps = m_shaders->GetVariant(u8"decal", shaders::ShaderStage::Fragment, shaders::ShaderFlags::None);
        if (vs == nullptr || ps == nullptr) { return nullptr; }
        rhi::ColorTargetState color{};
        color.format = kDecalHdrFormat;
        color.blend  = rhi::BlendState::AlphaBlend();
        rhi::FragmentState frag{}; frag.shader = rhi::ProgrammableStage{ ps, u8"main", rhi::ShaderStage::Fragment };
        frag.targets = Span<const rhi::ColorTargetState>{ &color, 1 };
        rhi::RenderPipelineDesc pd{};
        pd.layout = m_pipelineLayout;
        pd.vertex.shader = rhi::ProgrammableStage{ vs, u8"main", rhi::ShaderStage::Vertex };
        pd.fragment = frag;
        pd.primitive.topology = rhi::PrimitiveTopology::TriangleList;
        pd.primitive.cullMode = rhi::CullMode::None;   // fullscreen tri, no depth attachment
        pd.label = u8"decal";
        rhi::RenderPipeline* p = nullptr;
        if (!m_device->CreateRenderPipeline(pd, p).IsOk()) { return nullptr; }
        return p;
    }

    // One bind group over the decal-uniform ring (dynamic offset per decal). Rebuilt only on ring
    // realloc (generation), which drains the GPU first — never freeing an in-flight set.
    rhi::BindGroup* EnsureUboBindGroup() {
        const u32 gen = m_decalRing.Generation();
        if (m_uboBg != nullptr && m_uboBgGen == gen) { return m_uboBg; }
        if (m_uboBg != nullptr) { m_device->DestroyBindGroup(m_uboBg); m_uboBg = nullptr; }
        if (m_decalRing.Buffer() == nullptr) { return nullptr; }
        rhi::BindGroupEntry e = rhi::BindGroupEntry::BufferEntry(m_decalRing.Buffer(), 0, m_decalRing.SlotSize());
        rhi::BindGroupDesc bgd{}; bgd.layout = m_uboLayout; bgd.entries = Span<const rhi::BindGroupEntry>{ &e, 1 };
        if (!m_device->CreateBindGroup(bgd, m_uboBg).IsOk()) { m_uboBg = nullptr; return nullptr; }
        m_uboBgGen = gen;
        return m_uboBg;
    }

    // Depth bind group (set 0), cached by (view, generation) with a defer-free retire list — the depth
    // transient is aliased/recreated, so a raw-pointer cache would free a set an in-flight frame still uses.
    rhi::BindGroup* EnsureDepthBindGroup(rhi::TextureView* depth, u64 generation) {
        if (depth == nullptr) { return nullptr; }
        if (m_depthBg != nullptr && m_depthView == depth && m_depthGen == generation) { return m_depthBg; }
        if (m_depthBg != nullptr) { m_retired.PushBack(Retired{ m_depthBg, kRetireFrames }); m_depthBg = nullptr; }
        rhi::BindGroupEntry ent[] = {
            rhi::BindGroupEntry::TextureEntry(depth), rhi::BindGroupEntry::SamplerEntry(m_depthSampler),
        };
        rhi::BindGroupDesc bgd{}; bgd.layout = m_depthLayout; bgd.entries = Span<const rhi::BindGroupEntry>{ ent, 2 };
        if (!m_device->CreateBindGroup(bgd, m_depthBg).IsOk()) { m_depthBg = nullptr; return nullptr; }
        m_depthView = depth; m_depthGen = generation;
        return m_depthBg;
    }

    rhi::BindGroup* EnsureTextureBindGroup(rhi::TextureView* tex) {
        if (tex == nullptr) { return nullptr; }
        if (rhi::BindGroup** found = m_texBindGroups.Find(tex)) { return *found; }
        rhi::BindGroupEntry ent[] = {
            rhi::BindGroupEntry::TextureEntry(tex), rhi::BindGroupEntry::SamplerEntry(m_texSampler),
        };
        rhi::BindGroupDesc bgd{}; bgd.layout = m_texLayout; bgd.entries = Span<const rhi::BindGroupEntry>{ ent, 2 };
        rhi::BindGroup* bg = nullptr;
        if (!m_device->CreateBindGroup(bgd, bg).IsOk()) { return nullptr; }
        m_texBindGroups.InsertOrAssign(tex, bg);
        return bg;
    }

    void Tick(u32 frameIndex) {
        if (frameIndex == m_lastFrame) { return; }
        m_lastFrame = frameIndex;
        usize w = 0;
        for (usize i = 0; i < m_retired.Size(); ++i) {
            if (m_retired[i].left <= 1) { m_device->DestroyBindGroup(m_retired[i].bg); }
            else { m_retired[i].left -= 1; m_retired[w++] = m_retired[i]; }
        }
        m_retired.Resize(w);
    }

    void Shutdown() {
        for (auto& kv : m_texBindGroups) { if (kv.value != nullptr) { m_device->DestroyBindGroup(kv.value); } }
        m_texBindGroups.Clear();
        for (Retired& r : m_retired) { if (r.bg != nullptr) { m_device->DestroyBindGroup(r.bg); } }
        m_retired.Clear();
        if (m_depthBg != nullptr) { m_device->DestroyBindGroup(m_depthBg); m_depthBg = nullptr; }
        if (m_uboBg != nullptr) { m_device->DestroyBindGroup(m_uboBg); m_uboBg = nullptr; }
        if (m_pipeline != nullptr) { m_device->DestroyRenderPipeline(m_pipeline); m_pipeline = nullptr; }
        if (m_texSampler != nullptr) { m_device->DestroySampler(m_texSampler); m_texSampler = nullptr; }
        if (m_depthSampler != nullptr) { m_device->DestroySampler(m_depthSampler); m_depthSampler = nullptr; }
        if (m_pipelineLayout != nullptr) { m_device->DestroyPipelineLayout(m_pipelineLayout); m_pipelineLayout = nullptr; }
        if (m_texLayout != nullptr) { m_device->DestroyBindGroupLayout(m_texLayout); m_texLayout = nullptr; }
        if (m_uboLayout != nullptr) { m_device->DestroyBindGroupLayout(m_uboLayout); m_uboLayout = nullptr; }
        if (m_depthLayout != nullptr) { m_device->DestroyBindGroupLayout(m_depthLayout); m_depthLayout = nullptr; }
    }

    struct Retired { rhi::BindGroup* bg = nullptr; u32 left = 0; };

    rhi::Device*           m_device;
    shaders::ShaderSystem* m_shaders;
    DynamicUniformRing     m_decalRing;
    rhi::BindGroupLayout*  m_depthLayout = nullptr;
    rhi::BindGroupLayout*  m_uboLayout = nullptr;
    rhi::BindGroupLayout*  m_texLayout = nullptr;
    rhi::PipelineLayout*   m_pipelineLayout = nullptr;
    rhi::RenderPipeline*   m_pipeline = nullptr;
    rhi::Sampler*          m_depthSampler = nullptr;
    rhi::Sampler*          m_texSampler = nullptr;
    rhi::BindGroup*        m_uboBg = nullptr;
    u32                    m_uboBgGen = 0xFFFFFFFFu;
    rhi::BindGroup*        m_depthBg = nullptr;
    rhi::TextureView*      m_depthView = nullptr;
    u64                    m_depthGen = 0;
    u32                    m_lastFrame = 0xFFFFFFFFu;
    bool                   m_reserved = false;
    Array<Retired>         m_retired;
    HashMap<rhi::TextureView*, rhi::BindGroup*> m_texBindGroups;
};

} // namespace draconic::render
