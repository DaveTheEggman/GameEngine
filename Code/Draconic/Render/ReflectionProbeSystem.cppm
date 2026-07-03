/// Draconic::Render — the `:probes` partition.
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
    Vec4 center;   // xyz = capture center (world),          w = intensity
    Vec4 boxMin;   // xyz = box min corner (world),          w = blendDistance
    Vec4 boxMax;   // xyz = box max corner (world),          w = sliceBase (float; cube index into the array)
    Vec4 params;   // x = mipCount, y = priority,            zw = pad
};
static_assert(sizeof(GpuProbe) == 64);

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
        return Status{};
    }

    // Map this frame's extracted probes onto persistent array slots (stable per ProbeKey) and build the
    // CPU-side GpuProbe records. Marks a probe dirty (needs capture) when it takes a NEW slot or its
    // transform changed since last assign. Returns the number of active probes. (Upload + capture happen
    // in P1b/P1c.) Probes beyond kMaxProbes are dropped (logged by the caller if it cares).
    u32 Assign(Span<const ReflectionProbe> probes) {
        m_active = 0;
        for (const ReflectionProbe& p : probes) {
            if (m_active >= kMaxProbes) { break; }
            const u32 slot = SlotFor(p.key);
            if (slot == kInvalidSlot) { continue; }

            const Vec3 boxMin = p.center - p.halfExtents;
            const Vec3 boxMax = p.center + p.halfExtents;

            GpuProbe g{};
            g.center = Vec4{ p.center.x, p.center.y, p.center.z, p.intensity };
            g.boxMin = Vec4{ boxMin.x, boxMin.y, boxMin.z, p.blendDistance };
            g.boxMax = Vec4{ boxMax.x, boxMax.y, boxMax.z, static_cast<f32>(slot) };
            g.params = Vec4{ static_cast<f32>(kPrefilterMips), static_cast<f32>(p.priority), 0.0f, 0.0f };
            m_cpuProbes[m_active] = g;

            // Dirty tracking for static caching: recapture on a new slot or a moved probe.
            const u64 sig = TransformSignature(p);
            SlotState& st = m_slotState[slot];
            if (!st.captured || st.signature != sig || p.update == ProbeUpdateMode::Realtime) {
                st.dirty = true;
            }
            st.signature = sig;
            st.probeKey  = p.key;
            ++m_active;
        }
        return m_active;
    }

    [[nodiscard]] u32 ActiveCount() const noexcept { return m_active; }
    [[nodiscard]] Span<const GpuProbe> CpuProbes() const noexcept {
        return Span<const GpuProbe>{ m_cpuProbes, m_active };
    }

    // Resources (consumed by the forward in P2, and by capture/prefilter in P1b/c).
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
        cd.usage = rhi::TextureUsage::RenderTarget | rhi::TextureUsage::Sampled;
        cd.label = u8"probes.captured";
        if (!m_device->CreateTexture(cd, m_capturedCube).IsOk()) { return false; }

        // Prefiltered specular cube-array (mip chain): GGX split-sum per probe, sampled by the forward.
        rhi::TextureDesc pd{};
        pd.format = kCubeFormat; pd.width = kPrefilterRes; pd.height = kPrefilterRes;
        pd.arrayLayerCount = layers; pd.mipLevelCount = kPrefilterMips;
        pd.usage = rhi::TextureUsage::RenderTarget | rhi::TextureUsage::Sampled;
        pd.label = u8"probes.prefilter";
        if (!m_device->CreateTexture(pd, m_prefilterCube).IsOk()) { return false; }
        rhi::TextureViewDesc pv{}; pv.format = kCubeFormat;
        pv.dimension = rhi::TextureViewDimension::TextureCubeArray;
        pv.arrayLayerCount = layers; pv.mipLevelCount = kPrefilterMips;
        if (!m_device->CreateTextureView(m_prefilterCube, pv, m_prefilterArrayView).IsOk()) { return false; }

        // Probe-metadata buffer (set-0 t9).
        rhi::BufferDesc bd{};
        bd.size = sizeof(GpuProbe) * kMaxProbes; bd.usage = rhi::BufferUsage::Storage;
        bd.memory = rhi::MemoryLocation::GpuOnly; bd.label = u8"probes.meta";
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

    void DestroyResources() {
        if (m_device == nullptr) { return; }
        if (m_prefilterArrayView != nullptr) { m_device->DestroyTextureView(m_prefilterArrayView); m_prefilterArrayView = nullptr; }
        if (m_prefilterCube != nullptr) { m_device->DestroyTexture(m_prefilterCube); m_prefilterCube = nullptr; }
        if (m_capturedCube != nullptr) { m_device->DestroyTexture(m_capturedCube); m_capturedCube = nullptr; }
        if (m_probeBuffer != nullptr) { m_device->DestroyBuffer(m_probeBuffer); m_probeBuffer = nullptr; }
        if (m_sampler != nullptr) { m_device->DestroySampler(m_sampler); m_sampler = nullptr; }
    }

    rhi::Device*           m_device  = nullptr;
    [[maybe_unused]] shaders::ShaderSystem* m_shaders = nullptr;   // prefilter pipelines (P1c)

    rhi::Texture*     m_capturedCube        = nullptr;
    rhi::Texture*     m_prefilterCube       = nullptr;
    rhi::TextureView* m_prefilterArrayView  = nullptr;
    rhi::Buffer*      m_probeBuffer         = nullptr;
    rhi::Sampler*     m_sampler             = nullptr;

    HashMap<u64, u32> m_slots;                    // ProbeKey -> array slot (persistent)
    u32               m_nextSlot = 0;
    SlotState         m_slotState[kMaxProbes];
    GpuProbe          m_cpuProbes[kMaxProbes];
    u32               m_active = 0;
};

} // namespace draconic::render
