/// Draconic::Render - the `:probes` partition.
///
/// Reflection probes: local, parallax-corrected, cluster-assigned cubemap reflections
/// (docs/design/reflection-probes.md). This system owns the per-probe GPU resources and (in later
/// sub-phases) drives capture + prefilter + froxel assignment:
///   - captured cube-ARRAY (RGBA16F, [maxProbes×6] layers)   : the raw 6-face scene capture per probe.
///   - prefiltered specular cube-ARRAY (RGBA16F, mip chain)  : GGX split-sum per probe, sampled by the
///                                                             forward (set 0, t8) with parallax.
///   - probe-metadata StructuredBuffer (GpuProbe[])          : center/box/blend/slice, sampled per froxel.
///
/// A stable ProbeKey (entity id) → slot map keeps a probe's array slice persistent across frames, so only
/// dirty probes re-capture (static caching). This sub-phase (P1a) allocates the resources + the slot
/// assignment; capture, prefilter, and the debug view land in P1b/P1c.

module;
#include "Core/Prelude.h"

export module draconic.render:probes;

import draconic.core;
import draconic.rhi;
import draconic.rendergraph;
import draconic.shaders;
import draconic.shaders.system;
import :data;   // ReflectionProbe / kMaxReflectionProbes / ProbeUpdateMode

using namespace draconic::core;
namespace rhi = draconic::rhi;

export namespace draconic::render {

// GPU-side probe record (set-0 t9 StructuredBuffer), 64 bytes. Packed so the forward can, per froxel,
// pick the probe, box-project the reflection ray, and index its cube-array slice.
struct GpuProbe {
    Float4 center;   // xyz = capture center (world),          w = intensity
    Float4 boxMin;   // xyz = box min corner (world),          w = blendDistance
    Float4 boxMax;   // xyz = box max corner (world),          w = sliceBase (float; cube index into the array)
    Float4 params;   // x = mipCount, y = priority,            zw = pad
};
static_assert(sizeof(GpuProbe) == 64);

// Fullscreen-triangle VS (uv from SV_VertexID) for the captured->prefiltered blit.
inline constexpr const char8_t* kProbeBlitVS = u8R"(
struct VSOut { float4 pos : SV_Position; float2 uv : TEXCOORD0; };
VSOut main(uint vid : SV_VertexID) {
    float2 uv = float2((vid << 1) & 2, vid & 2);
    VSOut o; o.uv = uv; o.pos = float4(uv * 2.0 - 1.0, 0.0, 1.0); return o;
}
)";

// Blit PS: copy one captured face into the prefiltered face, correcting the RH-LookAt horizontal mirror
// by flipping u. Samples the captured face as a plain Texture2D (NOT the cube sampler) so filtering never
// crosses a face boundary - the cube-sampler path shows the face seams in smooth gradients (sky). This is
// Sedulous's probe_blit. Image-space flip => winding stays correct (a camera-axis flip breaks culling).
inline constexpr const char8_t* kProbeBlitPS = u8R"(
Texture2D<float4> SrcFace : register(t0, space0);
SamplerState      Samp    : register(s0, space0);
float4 main(float4 pos : SV_Position, float2 uv : TEXCOORD0) : SV_Target0 {
    // flip-u corrects the RH-LookAt mirror; flip-v corrects the vertical inversion from the capture +
    // blit both passing through the negative viewport. (Retested after fixing the sky-slot collision that
    // had scrambled the earlier read.)
    return SrcFace.SampleLevel(Samp, float2(1.0 - uv.x, 1.0 - uv.y), 0.0);
}
)";

// GGX prefilter PS: convolve the CORRECTED probe cube (prefiltered mip 0) into a rougher mip. Karis
// split-sum importance sampling (same math as IBLSystem). Reads mip 0, writes mip M (roughness=M/(mips-1));
// the forward samples roughness*maxLod so rough surfaces get progressively blurrier reflections.
inline constexpr const char8_t* kProbePrefilterPS = u8R"(
struct Push { int FaceIndex; float Roughness; float2 Pad; };
[[vk::push_constant]] Push pc;
TextureCube<float4> Src  : register(t0, space0);
SamplerState        Samp : register(s0, space0);
static const float PI = 3.14159265359;
float3 DirForFace(int face, float2 uv) {
    float2 t = uv * 2.0 - 1.0;
    float3 d;
    if      (face == 0) d = float3( 1.0,  t.y, -t.x);
    else if (face == 1) d = float3(-1.0,  t.y,  t.x);
    else if (face == 2) d = float3( t.x,  1.0, -t.y);
    else if (face == 3) d = float3( t.x, -1.0,  t.y);
    else if (face == 4) d = float3( t.x,  t.y,  1.0);
    else                d = float3(-t.x,  t.y, -1.0);
    return normalize(d);
}
float RadicalInverse_VdC(uint bits) {
    bits = (bits << 16u) | (bits >> 16u);
    bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
    bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
    bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
    bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
    return float(bits) * 2.3283064365386963e-10;
}
float2 Hammersley(uint i, uint n) { return float2(float(i) / float(n), RadicalInverse_VdC(i)); }
float3 ImportanceSampleGGX(float2 xi, float3 n, float roughness) {
    float a = roughness * roughness;
    float phi = 2.0 * PI * xi.x;
    float cosT = sqrt((1.0 - xi.y) / (1.0 + (a * a - 1.0) * xi.y));
    float sinT = sqrt(1.0 - cosT * cosT);
    float3 h = float3(cos(phi) * sinT, sin(phi) * sinT, cosT);
    float3 up = abs(n.z) < 0.999 ? float3(0, 0, 1) : float3(1, 0, 0);
    float3 tx = normalize(cross(up, n));
    float3 ty = cross(n, tx);
    return normalize(tx * h.x + ty * h.y + n * h.z);
}
float4 main(float4 pos : SV_Position, float2 uv : TEXCOORD0) : SV_Target0 {
    float3 N = DirForFace(pc.FaceIndex, uv);
    float3 V = N;
    const uint SAMPLES = 128u;   // per-probe, may run every frame (Realtime) -> fewer than IBL's 1024
    float3 color = 0.0; float weight = 0.0;
    for (uint i = 0u; i < SAMPLES; ++i) {
        float2 xi = Hammersley(i, SAMPLES);
        float3 H  = ImportanceSampleGGX(xi, N, pc.Roughness);
        float3 L  = normalize(2.0 * dot(V, H) * H - V);
        float  ndl = dot(N, L);
        if (ndl > 0.0) {
            float3 s = Src.SampleLevel(Samp, L, 0.0).rgb;
            // Karis firefly reduction: down-weight bright samples (tone weight) so sparse importance-sample
            // hits on tiny bright sources (moving point lights in the low-res capture) don't alias/flicker.
            float fw = ndl / (1.0 + dot(s, float3(0.2126, 0.7152, 0.0722)));
            color += s * fw; weight += fw;
        }
    }
    return float4(color / max(weight, 1e-4), 1.0);
}
)";

struct PrefilterPush { i32 faceIndex = 0; f32 roughness = 0.0f; f32 pad0 = 0.0f, pad1 = 0.0f; };
static_assert(sizeof(PrefilterPush) == 16);

class ReflectionProbeSystem {
public:
    // One fixed array resolution for all probe slices (a cube-array can't vary per-slice; the component's
    // per-probe `resolution` is honored later via separate textures if needed).
    static constexpr u32 kCaptureRes   = 128;
    static constexpr u32 kPrefilterRes = 128;
    static constexpr u32 kPrefilterMips = 5;                  // roughness = mip / (mips - 1)
    static constexpr u32 kMaxProbes    = kMaxReflectionProbes;
    static constexpr rhi::TextureFormat kCubeFormat = rhi::TextureFormat::RGBA16Float;

    ReflectionProbeSystem(rhi::Device& device, shaders::ShaderSystem& shaders) noexcept
        : m_device(&device), m_shaders(&shaders) {}

    ~ReflectionProbeSystem() { DestroyResources(); }

    Status Initialize() {
        if (!CreateResources()) { return Status{ ErrorCode::Unknown }; }
        m_shaders->RegisterSource(u8"probe_blit_vs", shaders::ShaderStage::Vertex,   kProbeBlitVS);
        m_shaders->RegisterSource(u8"probe_blit_ps", shaders::ShaderStage::Fragment, kProbeBlitPS);
        m_shaders->RegisterSource(u8"probe_prefilter_ps", shaders::ShaderStage::Fragment, kProbePrefilterPS);
        if (!CreateBlitPipeline() || !CreatePrefilterPipeline()) { return Status{ ErrorCode::Unknown }; }
        return Status{};
    }

    // Map this frame's extracted probes onto persistent array slots (stable per ProbeKey) and build the
    // CPU-side GpuProbe records. Marks a probe dirty (needs capture) when it takes a NEW slot or its
    // transform changed since last assign. Returns the number of active probes. (Upload + capture happen
    // in P1b/P1c.) Probes beyond kMaxProbes are dropped (logged by the caller if it cares).
    u32 Assign(Span<const ReflectionProbe> probes) {
        m_active = 0;
        m_captures.Clear();
        for (const ReflectionProbe& p : probes) {
            if (m_active >= kMaxProbes) { break; }
            const u32 slot = SlotFor(p.key);
            if (slot == kInvalidSlot) { continue; }

            const Float3 boxMin = p.center - p.halfExtents;
            const Float3 boxMax = p.center + p.halfExtents;

            GpuProbe g{};
            g.center = Float4{ p.center.x, p.center.y, p.center.z, p.intensity };
            g.boxMin = Float4{ boxMin.x, boxMin.y, boxMin.z, p.blendDistance };
            g.boxMax = Float4{ boxMax.x, boxMax.y, boxMax.z, static_cast<f32>(slot) };
            g.params = Float4{ static_cast<f32>(kPrefilterMips), static_cast<f32>(p.priority), p.parallax ? 1.0f : 0.0f, 0.0f };
            m_cpuProbes[m_active] = g;

            // Dirty tracking for static caching: recapture on a new slot or a moved probe.
            const u64 sig = TransformSignature(p);
            SlotState& st = m_slotState[slot];
            if (!st.captured || st.signature != sig || p.update == ProbeUpdateMode::Realtime) {
                st.dirty = true;
            }
            st.signature = sig;
            st.probeKey  = p.key;
            if (st.dirty) { m_captures.PushBack(CaptureTask{ slot, p.center }); }
            ++m_active;
        }
        return m_active;
    }

    [[nodiscard]] u32 ActiveCount() const noexcept { return m_active; }
    [[nodiscard]] Span<const GpuProbe> CpuProbes() const noexcept {
        return Span<const GpuProbe>{ m_cpuProbes, m_active };
    }

    // Upload this frame's probe records into the metadata buffer (set-0 t9). Call once per frame after Assign.
    void Upload() {
        if (m_probeBuffer == nullptr || m_active == 0) { return; }
        if (void* p = m_probeBuffer->Map()) {
            MemCopy(p, m_cpuProbes, static_cast<usize>(m_active) * sizeof(GpuProbe));
            m_probeBuffer->Unmap();
        }
    }

    // A probe that needs (re)capture this frame: its array slot + world capture center. The capture loop
    // renders the scene into layers [LayerBase(slot) .. +6) then calls MarkCaptured(slot).
    struct CaptureTask { u32 slot; Float3 center; };
    [[nodiscard]] Span<const CaptureTask> Captures() const noexcept {
        return Span<const CaptureTask>{ m_captures.Data(), m_captures.Size() };
    }
    [[nodiscard]] static u32 LayerBase(u32 slot) noexcept { return slot * 6u; }
    void MarkCaptured(u32 slot) noexcept {
        if (slot < kMaxProbes) { m_slotState[slot].captured = true; m_slotState[slot].dirty = false; }
    }

    // Near/far planes for the 90°-FOV face cameras (cover the box interior out to distant geometry + sky).
    static constexpr f32 kCaptureNear = 0.1f;
    static constexpr f32 kCaptureFar  = 1000.0f;

    // Transition the WHOLE captured cube-array to ShaderRead once (out of graph, at first encoder hold),
    // so uncaptured slices aren't left UNDEFINED when the forward binds the whole-array SRV (same reason
    // the mesh renderer's dummy textures are pre-transitioned). Idempotent.
    void InitLayouts(rhi::CommandEncoder& encoder) {
        if (m_layoutsInit) { return; }
        encoder.TransitionTexture(m_capturedCube, rhi::ResourceState::Undefined, rhi::ResourceState::ShaderRead);
        encoder.TransitionTexture(m_prefilterCube, rhi::ResourceState::Undefined, rhi::ResourceState::ShaderRead);
        m_capturedState = rhi::ResourceState::ShaderRead;
        m_prefilterState = rhi::ResourceState::ShaderRead;
        m_layoutsInit = true;
    }

    // Import the captured cube-array into the graph (whole resource; the capture passes target individual
    // layers via subresource ranges). Persists its resource state across frames like the shadow atlas.
    rendergraph::RGHandle ImportCaptured(rendergraph::RenderGraph& graph) {
        const rendergraph::RGHandle h = graph.ImportTarget(
            u8"probes.captured", m_capturedCube, m_capturedArrayView,
            rhi::ResourceState::ShaderRead, m_capturedState);
        m_capturedState = rhi::ResourceState::ShaderRead;
        return h;
    }

    // Import the prefiltered cube-array (the SEPARATE texture the forward samples at t8 - never a capture
    // render target, so no read/write hazard with the capture passes; captured is copied into it below).
    rendergraph::RGHandle ImportPrefiltered(rendergraph::RenderGraph& graph) {
        const rendergraph::RGHandle h = graph.ImportTarget(
            u8"probes.prefilter", m_prefilterCube, m_prefilterArrayView,
            rhi::ResourceState::ShaderRead, m_prefilterState);
        m_prefilterState = rhi::ResourceState::ShaderRead;
        return h;
    }

    // Bridge captured -> prefiltered for one slot's 6 faces (mip 0), correcting the RH-LookAt horizontal
    // mirror via a flip blit (sharp for now; a GGX roughness convolution replaces this later). One render
    // pass per face samples the captured cube-array and writes the prefiltered layer; the graph orders
    // capture-write -> blit-read + blit-write -> forward-read.
    void DeclareBlit(rendergraph::RenderGraph& graph, rendergraph::RGHandle capturedH,
                     rendergraph::RGHandle prefilteredH, u32 slot) {
        for (u32 face = 0; face < 6; ++face) {
            rhi::BindGroup* faceBG = EnsureFaceBlit(slot, face);
            if (faceBG == nullptr) { continue; }
            graph.AddRenderPass(u8"probes.blit", [this, capturedH, prefilteredH, slot, face, faceBG](rendergraph::PassBuilder& b) {
                rendergraph::RGSubresourceRange sub{}; sub.baseArrayLayer = slot * 6u + face; sub.arrayLayerCount = 1;
                b.SetColorTarget(0, prefilteredH, rhi::LoadOp::Clear, rhi::StoreOp::Store, rhi::ClearColor::Black(), sub);
                b.ReadTexture(capturedH);
                b.SetViewport(0, 0, kPrefilterRes, kPrefilterRes);
                b.NeverCull();
                b.SetExecute([this, faceBG](rhi::RenderPassEncoder& rp) {
                    rp.SetPipeline(m_blitPipeline);
                    rp.SetBindGroup(0, faceBG, Span<const u32>{});
                    rp.Draw(3, 1, 0, 0);
                });
            });
        }
    }

    // GGX-convolve prefiltered mip 0 (the corrected cube) into mips 1..N-1 (roughness = mip/(mips-1)).
    // Reads mip 0, writes each rougher mip/face; the graph orders after DeclareBlit (which wrote mip 0)
    // via the shared prefilteredH. The forward samples roughness*(mips-1) -> blurrier at higher roughness.
    void DeclarePrefilter(rendergraph::RenderGraph& graph, rendergraph::RGHandle prefilteredH, u32 slot) {
        rhi::BindGroup* srcBG = EnsurePrefilterSource(slot);
        if (srcBG == nullptr) { return; }
        for (u32 mip = 1; mip < kPrefilterMips; ++mip) {
            const f32 roughness = static_cast<f32>(mip) / static_cast<f32>(kPrefilterMips - 1);
            const u32 mipRes = kPrefilterRes >> mip;
            for (u32 face = 0; face < 6; ++face) {
                graph.AddRenderPass(u8"probes.prefilter", [this, prefilteredH, slot, mip, face, roughness, mipRes, srcBG](rendergraph::PassBuilder& b) {
                    rendergraph::RGSubresourceRange src{}; src.baseMipLevel = 0;   src.mipLevelCount = 1; src.baseArrayLayer = slot * 6u;        src.arrayLayerCount = 6;
                    rendergraph::RGSubresourceRange dst{}; dst.baseMipLevel = mip; dst.mipLevelCount = 1; dst.baseArrayLayer = slot * 6u + face; dst.arrayLayerCount = 1;
                    b.ReadTexture(prefilteredH, src);
                    b.SetColorTarget(0, prefilteredH, rhi::LoadOp::Clear, rhi::StoreOp::Store, rhi::ClearColor::Black(), dst);
                    b.SetViewport(0, 0, mipRes, mipRes);
                    b.NeverCull();
                    b.SetExecute([this, face, roughness, srcBG](rhi::RenderPassEncoder& rp) {
                        PrefilterPush push{}; push.faceIndex = static_cast<i32>(face); push.roughness = roughness;
                        rp.SetPipeline(m_prefilterPipeline);
                        rp.SetBindGroup(0, srcBG, Span<const u32>{});
                        rp.SetPushConstants(rhi::ShaderStage::Fragment, 0, sizeof(PrefilterPush), &push);
                        rp.Draw(3, 1, 0, 0);
                    });
                });
            }
        }
    }

    // Resources (consumed by the forward in P2, and by capture/prefilter in P1b/c).
    // The captured cube-ARRAY as a sample view (set-0 t8; P2 samples this directly, sharp mip 0). Reuses
    // the whole-array view used for the render-target import - a stable, single-allocation view.
    [[nodiscard]] rhi::TextureView* CapturedSampleView() const noexcept { return m_capturedArrayView; }
    [[nodiscard]] rhi::TextureView* PrefilterArrayView() const noexcept { return m_prefilterArrayView; }
    [[nodiscard]] rhi::Buffer*      ProbeBuffer()        const noexcept { return m_probeBuffer; }
    [[nodiscard]] rhi::Sampler*     Sampler()            const noexcept { return m_sampler; }

private:
    static constexpr u32 kInvalidSlot = 0xFFFFFFFFu;

    struct SlotState {
        u64  probeKey  = 0;
        u64  signature = 0;
        bool captured  = false;   // a full cube has been captured + prefiltered at least once
        bool dirty     = false;   // needs (re)capture this frame
    };

    // Stable slot for a probe key: reuse if seen, else claim the next free slot (up to kMaxProbes).
    u32 SlotFor(u64 key) {
        if (const u32* found = m_slots.Find(key)) { return *found; }
        if (m_nextSlot >= kMaxProbes) { return kInvalidSlot; }
        const u32 slot = m_nextSlot++;
        m_slots.InsertOrAssign(key, slot);
        return slot;
    }

    // Cheap change signature over a probe's captured-input state (transform + box + resolution). Bit-mixed
    // hash of the floats; exact equality is fine here (we only need "changed vs last frame").
    [[nodiscard]] static u64 TransformSignature(const ReflectionProbe& p) {
        u64 h = 1469598103934665603ull;   // FNV-1a
        auto mix = [&](f32 v) { h ^= static_cast<u64>(__builtin_bit_cast(u32, v)); h *= 1099511628211ull; };
        mix(p.center.x); mix(p.center.y); mix(p.center.z);
        mix(p.halfExtents.x); mix(p.halfExtents.y); mix(p.halfExtents.z);
        h ^= static_cast<u64>(p.resolution);
        return h;
    }

    bool CreateResources() {
        const u32 layers = kMaxProbes * 6u;

        // Captured cube-array: the raw 6-face scene render per probe (RenderTarget), sampled by prefilter.
        rhi::TextureDesc cd{};
        cd.format = kCubeFormat; cd.width = kCaptureRes; cd.height = kCaptureRes;
        cd.arrayLayerCount = layers; cd.mipLevelCount = 1;
        cd.usage = rhi::TextureUsage::RenderTarget | rhi::TextureUsage::Sampled | rhi::TextureUsage::CopySrc;
        cd.label = u8"probes.captured";
        if (!m_device->CreateTexture(cd, m_capturedCube).IsOk()) { return false; }
        rhi::TextureViewDesc cv{}; cv.format = kCubeFormat;
        cv.dimension = rhi::TextureViewDimension::TextureCubeArray;
        cv.arrayLayerCount = layers; cv.mipLevelCount = 1;
        if (!m_device->CreateTextureView(m_capturedCube, cv, m_capturedArrayView).IsOk()) { return false; }

        // Prefiltered specular cube-array (mip chain): GGX split-sum per probe, sampled by the forward.
        rhi::TextureDesc pd{};
        pd.format = kCubeFormat; pd.width = kPrefilterRes; pd.height = kPrefilterRes;
        pd.arrayLayerCount = layers; pd.mipLevelCount = kPrefilterMips;
        pd.usage = rhi::TextureUsage::RenderTarget | rhi::TextureUsage::Sampled | rhi::TextureUsage::CopyDst;
        pd.label = u8"probes.prefilter";
        if (!m_device->CreateTexture(pd, m_prefilterCube).IsOk()) { return false; }
        rhi::TextureViewDesc pv{}; pv.format = kCubeFormat;
        pv.dimension = rhi::TextureViewDimension::TextureCubeArray;
        pv.arrayLayerCount = layers; pv.mipLevelCount = kPrefilterMips;
        if (!m_device->CreateTextureView(m_prefilterCube, pv, m_prefilterArrayView).IsOk()) { return false; }

        // Probe-metadata buffer (set-0 t9, StructuredBuffer<GpuProbe>). Host-visible so the forward reads
        // this frame's probes directly (small: kMaxProbes*64B); uploaded each frame in Upload().
        rhi::BufferDesc bd{};
        bd.size = sizeof(GpuProbe) * kMaxProbes; bd.usage = rhi::BufferUsage::Storage;
        bd.memory = rhi::MemoryLocation::CpuToGpu; bd.label = u8"probes.meta";
        if (!m_device->CreateBuffer(bd, m_probeBuffer).IsOk()) { return false; }

        // Linear-clamp sampler.
        rhi::SamplerDesc ss{};
        ss.minFilter = rhi::FilterMode::Linear; ss.magFilter = rhi::FilterMode::Linear;
        ss.mipmapFilter = rhi::MipmapFilterMode::Linear;
        ss.addressU = rhi::AddressMode::ClampToEdge; ss.addressV = rhi::AddressMode::ClampToEdge;
        ss.addressW = rhi::AddressMode::ClampToEdge; ss.label = u8"probes.sampler";
        if (!m_device->CreateSampler(ss, m_sampler).IsOk()) { return false; }

        return true;
    }

    bool CreateBlitPipeline() {
        rhi::ShaderModule* vs = m_shaders->GetVariant(u8"probe_blit_vs", shaders::ShaderStage::Vertex, shaders::ShaderFlags::None);
        rhi::ShaderModule* ps = m_shaders->GetVariant(u8"probe_blit_ps", shaders::ShaderStage::Fragment, shaders::ShaderFlags::None);
        if (vs == nullptr || ps == nullptr) { return false; }

        rhi::BindGroupLayoutEntry srcTex  = rhi::BindGroupLayoutEntry::SampledTexture(0, rhi::ShaderStage::Fragment, rhi::TextureViewDimension::Texture2D);
        rhi::BindGroupLayoutEntry srcSamp = rhi::BindGroupLayoutEntry::Sampler(0, rhi::ShaderStage::Fragment);
        rhi::BindGroupLayoutEntry entries[] = { srcTex, srcSamp };
        rhi::BindGroupLayoutDesc ld{}; ld.entries = Span<const rhi::BindGroupLayoutEntry>{ entries, 2 };
        if (!m_device->CreateBindGroupLayout(ld, m_blitLayout).IsOk()) { return false; }

        rhi::BindGroupLayout* layouts[] = { m_blitLayout };
        rhi::PipelineLayoutDesc pld{};
        pld.bindGroupLayouts = Span<rhi::BindGroupLayout* const>{ layouts, 1 };
        if (!m_device->CreatePipelineLayout(pld, m_blitPipeLayout).IsOk()) { return false; }

        rhi::ColorTargetState color{}; color.format = kCubeFormat;
        rhi::FragmentState frag{}; frag.shader = rhi::ProgrammableStage{ ps, u8"main", rhi::ShaderStage::Fragment };
        frag.targets = Span<const rhi::ColorTargetState>{ &color, 1 };
        rhi::RenderPipelineDesc pd{};
        pd.layout = m_blitPipeLayout;
        pd.vertex.shader = rhi::ProgrammableStage{ vs, u8"main", rhi::ShaderStage::Vertex };
        pd.fragment = frag;
        pd.primitive.topology = rhi::PrimitiveTopology::TriangleList;
        pd.primitive.cullMode = rhi::CullMode::None;
        pd.label = u8"probes.blit";
        if (!m_device->CreateRenderPipeline(pd, m_blitPipeline).IsOk()) { return false; }
        return true;
    }

    // Lazily create (and cache) a probe slot+face's captured 2D-layer SRV + its blit bind group. Sampling
    // the single face layer as a Texture2D (not the cube) keeps filtering inside the face -> no seams.
    rhi::BindGroup* EnsureFaceBlit(u32 slot, u32 face) {
        const u32 idx = slot * 6u + face;
        if (m_blitFaceBG[idx] != nullptr) { return m_blitFaceBG[idx]; }
        rhi::TextureViewDesc vd{}; vd.format = kCubeFormat;
        vd.dimension = rhi::TextureViewDimension::Texture2D;
        vd.baseArrayLayer = idx; vd.arrayLayerCount = 1; vd.mipLevelCount = 1;
        if (!m_device->CreateTextureView(m_capturedCube, vd, m_capturedFaceView[idx]).IsOk()) { return nullptr; }
        rhi::BindGroupEntry be[] = { rhi::BindGroupEntry::TextureEntry(m_capturedFaceView[idx]), rhi::BindGroupEntry::SamplerEntry(m_sampler) };
        rhi::BindGroupDesc bgd{}; bgd.layout = m_blitLayout; bgd.entries = Span<const rhi::BindGroupEntry>{ be, 2 };
        if (!m_device->CreateBindGroup(bgd, m_blitFaceBG[idx]).IsOk()) { m_blitFaceBG[idx] = nullptr; return nullptr; }
        return m_blitFaceBG[idx];
    }

    bool CreatePrefilterPipeline() {
        rhi::ShaderModule* vs = m_shaders->GetVariant(u8"probe_blit_vs", shaders::ShaderStage::Vertex, shaders::ShaderFlags::None);
        rhi::ShaderModule* ps = m_shaders->GetVariant(u8"probe_prefilter_ps", shaders::ShaderStage::Fragment, shaders::ShaderFlags::None);
        if (vs == nullptr || ps == nullptr) { return false; }

        rhi::BindGroupLayoutEntry srcTex  = rhi::BindGroupLayoutEntry::SampledTexture(0, rhi::ShaderStage::Fragment, rhi::TextureViewDimension::TextureCube);
        rhi::BindGroupLayoutEntry srcSamp = rhi::BindGroupLayoutEntry::Sampler(0, rhi::ShaderStage::Fragment);
        rhi::BindGroupLayoutEntry entries[] = { srcTex, srcSamp };
        rhi::BindGroupLayoutDesc ld{}; ld.entries = Span<const rhi::BindGroupLayoutEntry>{ entries, 2 };
        if (!m_device->CreateBindGroupLayout(ld, m_prefilterLayout).IsOk()) { return false; }

        rhi::PushConstantRange pcRange{}; pcRange.stages = rhi::ShaderStage::Fragment; pcRange.offset = 0; pcRange.size = sizeof(PrefilterPush);
        rhi::BindGroupLayout* layouts[] = { m_prefilterLayout };
        rhi::PipelineLayoutDesc pld{};
        pld.bindGroupLayouts   = Span<rhi::BindGroupLayout* const>{ layouts, 1 };
        pld.pushConstantRanges = Span<const rhi::PushConstantRange>{ &pcRange, 1 };
        if (!m_device->CreatePipelineLayout(pld, m_prefilterPipeLayout).IsOk()) { return false; }

        rhi::ColorTargetState color{}; color.format = kCubeFormat;
        rhi::FragmentState frag{}; frag.shader = rhi::ProgrammableStage{ ps, u8"main", rhi::ShaderStage::Fragment };
        frag.targets = Span<const rhi::ColorTargetState>{ &color, 1 };
        rhi::RenderPipelineDesc pd{};
        pd.layout = m_prefilterPipeLayout;
        pd.vertex.shader = rhi::ProgrammableStage{ vs, u8"main", rhi::ShaderStage::Vertex };
        pd.fragment = frag;
        pd.primitive.topology = rhi::PrimitiveTopology::TriangleList;
        pd.primitive.cullMode = rhi::CullMode::None;
        pd.label = u8"probes.prefilter";
        if (!m_device->CreateRenderPipeline(pd, m_prefilterPipeline).IsOk()) { return false; }
        return true;
    }

    // Per-slot source cube view (prefiltered mip 0 as a TextureCube) + its prefilter bind group.
    rhi::BindGroup* EnsurePrefilterSource(u32 slot) {
        if (m_prefilterSrcBG[slot] != nullptr) { return m_prefilterSrcBG[slot]; }
        rhi::TextureViewDesc vd{}; vd.format = kCubeFormat;
        vd.dimension = rhi::TextureViewDimension::TextureCube;
        vd.baseMipLevel = 0; vd.mipLevelCount = 1;
        vd.baseArrayLayer = slot * 6u; vd.arrayLayerCount = 6;
        if (!m_device->CreateTextureView(m_prefilterCube, vd, m_prefilterSrcView[slot]).IsOk()) { return nullptr; }
        rhi::BindGroupEntry be[] = { rhi::BindGroupEntry::TextureEntry(m_prefilterSrcView[slot]), rhi::BindGroupEntry::SamplerEntry(m_sampler) };
        rhi::BindGroupDesc bgd{}; bgd.layout = m_prefilterLayout; bgd.entries = Span<const rhi::BindGroupEntry>{ be, 2 };
        if (!m_device->CreateBindGroup(bgd, m_prefilterSrcBG[slot]).IsOk()) { m_prefilterSrcBG[slot] = nullptr; return nullptr; }
        return m_prefilterSrcBG[slot];
    }


    void DestroyResources() {
        if (m_device == nullptr) { return; }
        for (u32 i = 0; i < kMaxProbes * 6; ++i) {
            if (m_blitFaceBG[i] != nullptr)       { m_device->DestroyBindGroup(m_blitFaceBG[i]); m_blitFaceBG[i] = nullptr; }
            if (m_capturedFaceView[i] != nullptr) { m_device->DestroyTextureView(m_capturedFaceView[i]); m_capturedFaceView[i] = nullptr; }
        }
        for (u32 i = 0; i < kMaxProbes; ++i) {
            if (m_prefilterSrcBG[i] != nullptr)   { m_device->DestroyBindGroup(m_prefilterSrcBG[i]); m_prefilterSrcBG[i] = nullptr; }
            if (m_prefilterSrcView[i] != nullptr) { m_device->DestroyTextureView(m_prefilterSrcView[i]); m_prefilterSrcView[i] = nullptr; }
        }
        if (m_prefilterPipeline != nullptr)   { m_device->DestroyRenderPipeline(m_prefilterPipeline); m_prefilterPipeline = nullptr; }
        if (m_prefilterPipeLayout != nullptr) { m_device->DestroyPipelineLayout(m_prefilterPipeLayout); m_prefilterPipeLayout = nullptr; }
        if (m_prefilterLayout != nullptr)     { m_device->DestroyBindGroupLayout(m_prefilterLayout); m_prefilterLayout = nullptr; }
        if (m_blitPipeline != nullptr)   { m_device->DestroyRenderPipeline(m_blitPipeline); m_blitPipeline = nullptr; }
        if (m_blitPipeLayout != nullptr) { m_device->DestroyPipelineLayout(m_blitPipeLayout); m_blitPipeLayout = nullptr; }
        if (m_blitLayout != nullptr)     { m_device->DestroyBindGroupLayout(m_blitLayout); m_blitLayout = nullptr; }
        if (m_prefilterArrayView != nullptr) { m_device->DestroyTextureView(m_prefilterArrayView); m_prefilterArrayView = nullptr; }
        if (m_prefilterCube != nullptr) { m_device->DestroyTexture(m_prefilterCube); m_prefilterCube = nullptr; }
        if (m_capturedArrayView != nullptr) { m_device->DestroyTextureView(m_capturedArrayView); m_capturedArrayView = nullptr; }
        if (m_capturedCube != nullptr) { m_device->DestroyTexture(m_capturedCube); m_capturedCube = nullptr; }
        if (m_probeBuffer != nullptr) { m_device->DestroyBuffer(m_probeBuffer); m_probeBuffer = nullptr; }
        if (m_sampler != nullptr) { m_device->DestroySampler(m_sampler); m_sampler = nullptr; }
    }

    rhi::Device*           m_device  = nullptr;
    shaders::ShaderSystem* m_shaders = nullptr;

    // Captured -> prefiltered flip-blit (corrects RH-LookAt mirror). Per-face 2D SRVs + bind groups so
    // filtering stays within a face (no cross-face seams in smooth gradients like the sky).
    rhi::BindGroupLayout* m_blitLayout     = nullptr;
    rhi::PipelineLayout*  m_blitPipeLayout = nullptr;
    rhi::RenderPipeline*  m_blitPipeline   = nullptr;
    rhi::TextureView*     m_capturedFaceView[kMaxProbes * 6] = {};
    rhi::BindGroup*       m_blitFaceBG[kMaxProbes * 6]       = {};

    // GGX roughness prefilter (prefiltered mip 0 -> mips 1..N-1).
    rhi::BindGroupLayout* m_prefilterLayout     = nullptr;
    rhi::PipelineLayout*  m_prefilterPipeLayout = nullptr;
    rhi::RenderPipeline*  m_prefilterPipeline   = nullptr;
    rhi::TextureView*     m_prefilterSrcView[kMaxProbes] = {};   // per-slot mip-0 cube view (convolution source)
    rhi::BindGroup*       m_prefilterSrcBG[kMaxProbes]   = {};

    rhi::Texture*     m_capturedCube        = nullptr;
    rhi::TextureView* m_capturedArrayView   = nullptr;
    rhi::ResourceState m_capturedState      = rhi::ResourceState::Undefined;   // persists across frames (import)
    bool              m_layoutsInit         = false;                           // one-time whole-array ShaderRead init
    rhi::Texture*     m_prefilterCube       = nullptr;
    rhi::TextureView* m_prefilterArrayView  = nullptr;
    rhi::ResourceState m_prefilterState     = rhi::ResourceState::Undefined;   // persists across frames (import)
    rhi::Buffer*      m_probeBuffer         = nullptr;
    rhi::Sampler*     m_sampler             = nullptr;

    Array<CaptureTask> m_captures;   // dirty probes to (re)capture this frame

    HashMap<u64, u32> m_slots;                    // ProbeKey -> array slot (persistent)
    u32               m_nextSlot = 0;
    SlotState         m_slotState[kMaxProbes];
    GpuProbe          m_cpuProbes[kMaxProbes];
    u32               m_active = 0;
};

} // namespace draconic::render
