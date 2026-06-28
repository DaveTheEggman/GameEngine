/// Raptor::Render — the `:shadows` partition.
///
/// Shadow mapping (phase 5). 5.1 is the directional vertical slice: a single shadow map rendered
/// from the scene's directional shadow caster's point of view, sampled with PCF in the forward
/// shader. This partition OWNS the shadow depth texture(s) (one per frame-in-flight) and imports
/// them into the frame graph; RenderFrame (:pipeline, which has the renderer registry) declares the
/// depth pass that re-emits the casters and threads the binding into the forward pass. CSM cascades
/// (5.2) + the rebuilt atlas/scheduler (5.3/5.4, modeled on PlayCanvas) extend this.

module;
#include "Core/Prelude.h"

export module raptor.render:shadows;

import raptor.core;
import raptor.rhi;
import raptor.rendergraph;
import :data;       // ShadowCascades
import :views;      // ViewCamera

using namespace raptor::core;
namespace rhi = raptor::rhi;

export namespace raptor::render {

// Unproject an NDC corner (clip xy in [-1,1], z in [0,1]) to world space via the inverse view-proj.
[[nodiscard]] inline Vec3 UnprojectNDC(const Mat4& invViewProj, f32 ndcX, f32 ndcY, f32 ndcZ) {
    const Vec4 c = Vec4{ ndcX, ndcY, ndcZ, 1.0f } * invViewProj;
    const f32 invW = (Abs(c.w) > 1e-6f) ? (1.0f / c.w) : 1.0f;
    return Vec3{ c.x * invW, c.y * invW, c.z * invW };
}

// Fit CSM cascades to the camera frustum. Practical split (lambda blend of log + uniform), a bounding
// SPHERE fit per cascade (stable under camera rotation) with radius snapping, plus light-space TEXEL
// snapping — the anti-shimmer fix Sedulous lacks (the survey flagged it). `lightDir` is the direction
// light travels; the light camera looks along it. Cascades cover [near, shadowDistance].
[[nodiscard]] inline ShadowCascades ComputeCascades(const ViewCamera& cam, Vec3 lightDir,
                                                    f32 shadowDistance, u32 resolution) {
    ShadowCascades out;
    out.valid = true;
    constexpr u32 N = ShadowCascades::kCount;
    const f32 nearZ = 0.1f;                              // camera near (matches the default projection)
    const f32 farZ  = Max(nearZ + 1.0f, shadowDistance); // CSM coverage range (splits span [near, this])
    // The frustum corners below span the CAMERA's full depth range, so split depths must be turned
    // into fractions of THAT range (camFar), not of the shorter shadow range (farZ).
    const f32 camFar = Max(farZ, cam.farZ);

    constexpr f32 lambda = 0.5f;   // practical split: 0 = uniform, 1 = logarithmic
    f32 splits[N + 1];
    splits[0] = nearZ;
    for (u32 i = 1; i <= N; ++i) {
        const f32 p    = static_cast<f32>(i) / static_cast<f32>(N);
        const f32 logS = nearZ * Pow(farZ / nearZ, p);
        const f32 uniS = nearZ + (farZ - nearZ) * p;
        splits[i] = lambda * logS + (1.0f - lambda) * uniS;
    }

    const Mat4 invVP = Inverse(cam.ViewProjection());
    Vec3 nearC[4], farC[4];
    const f32 xs[4] = { -1.0f, 1.0f, 1.0f, -1.0f };
    const f32 ys[4] = { -1.0f, -1.0f, 1.0f, 1.0f };
    for (int i = 0; i < 4; ++i) {
        nearC[i] = UnprojectNDC(invVP, xs[i], ys[i], 0.0f);
        farC[i]  = UnprojectNDC(invVP, xs[i], ys[i], 1.0f);
    }

    const Vec3 dir = Normalized(lightDir);
    const Vec3 up  = (Abs(dir.y) > 0.95f) ? Vec3{ 0.0f, 0.0f, 1.0f } : Vec3{ 0.0f, 1.0f, 0.0f };

    for (u32 c = 0; c < N; ++c) {
        // Slice corners: interpolate the camera frustum edges (near->far) by each split's fraction of
        // the camera's depth range (camFar), so a near split gives a SMALL near-cascade slice.
        const f32 fNear = (splits[c]     - nearZ) / (camFar - nearZ);
        const f32 fFar  = (splits[c + 1] - nearZ) / (camFar - nearZ);
        Vec3 corners[8];
        for (int i = 0; i < 4; ++i) {
            const Vec3 edge = farC[i] - nearC[i];
            corners[i]     = nearC[i] + edge * fNear;
            corners[i + 4] = nearC[i] + edge * fFar;
        }

        // Bounding sphere of the slice.
        Vec3 center{ 0.0f, 0.0f, 0.0f };
        for (int i = 0; i < 8; ++i) { center = center + corners[i]; }
        center = center * (1.0f / 8.0f);
        f32 radius = 0.0f;
        for (int i = 0; i < 8; ++i) { radius = Max(radius, Length(corners[i] - center)); }
        radius = Ceil(radius * 16.0f) / 16.0f;   // snap radius (reduces size shimmer)

        out.texelWorldSize[c] = (radius * 2.0f) / static_cast<f32>(resolution);
        out.splitFar[c]       = splits[c + 1];

        const f32  shadowDepth = radius * 6.0f;
        const Vec3 eye  = center - dir * (radius * 3.0f);
        const Mat4 view = Mat4::LookAtRH(eye, center, up);
        const Mat4 proj = Mat4::OrthographicRH(radius * 2.0f, radius * 2.0f, 0.0f, shadowDepth);
        Mat4 vp = view * proj;

        // Texel snap: shift the cascade so its origin lands on whole-texel increments in light clip
        // space, so the shadow texels step in integer units as the camera moves (no edge crawl).
        const Vec3 originClip = TransformPoint(Vec3{ 0.0f, 0.0f, 0.0f }, vp);   // ortho => w=1, no divide
        const f32  half = static_cast<f32>(resolution) * 0.5f;
        const f32  ox = (Floor(originClip.x * half + 0.5f) - originClip.x * half) / half;
        const f32  oy = (Floor(originClip.y * half + 0.5f) - originClip.y * half) / half;
        vp.m[3][0] += ox;
        vp.m[3][1] += oy;

        out.viewProj[c] = vp;
    }
    return out;
}

class ShadowSystem {
public:
    ShadowSystem(rhi::Device& device, u32 framesInFlight) noexcept
        : m_device(&device), m_framesInFlight(framesInFlight < 1 ? 1 : framesInFlight) {}

    ~ShadowSystem() { Shutdown(); }
    ShadowSystem(const ShadowSystem&) = delete;
    ShadowSystem& operator=(const ShadowSystem&) = delete;

    // Depth textures are created lazily on first use (one per frame-in-flight); the comparison
    // sampler that reads them lives in the MeshRenderer (it owns set 0 where the map is bound).
    Status Initialize() { return Status{}; }

    [[nodiscard]] rhi::TextureFormat Format()     const noexcept { return kShadowFormat; }
    [[nodiscard]] u32                Resolution() const noexcept { return kShadowResolution; }

    static constexpr u32 kMaxShadowViews = 4;   // per-view cascades; array = views * cascades layers

    // Ensure this frame's shadow array exists with enough layers for `viewCount` views' cascades
    // (called before the mesh renderer builds set 0, so the map view is available to bind). Returns
    // the array sample view, or null on failure.
    rhi::TextureView* PrepareFrame(u32 frameIndex, u32 viewCount) {
        const u32 slot   = frameIndex % m_framesInFlight;
        const u32 views  = (viewCount < 1) ? 1u : (viewCount > kMaxShadowViews ? kMaxShadowViews : viewCount);
        const u32 layers = views * kCascadeCount;
        return EnsureTexture(slot, layers) ? m_sampleViews[slot] : nullptr;
    }

    [[nodiscard]] rhi::TextureView* SampleView(u32 frameIndex) const noexcept {
        return m_sampleViews[frameIndex % m_framesInFlight];
    }

    // Bumped whenever a shadow texture is (re)created (lazily in 5.1; on resolution/atlas changes in
    // 5.2+). A consumer caching a bind group over SampleView keys on this so a reused-address view
    // can't alias a destroyed texture — same reason the cluster buffers carry a version.
    [[nodiscard]] u64 Generation() const noexcept { return m_generation; }

    // Import this frame's shadow texture into the graph: the depth pass writes it, then it barriers to
    // DepthStencilRead so the forward pass samples it. Returns the graph handle (invalid if no texture).
    rendergraph::RGHandle ImportTarget(rendergraph::RenderGraph& graph, u32 frameIndex) {
        const u32 slot = frameIndex % m_framesInFlight;
        if (m_textures[slot] == nullptr) { return {}; }   // PrepareFrame creates it (sized for the views)
        const rendergraph::RGHandle h = graph.ImportTarget(
            u8"shadow.map", m_textures[slot], m_attachViews[slot], m_sampleViews[slot],
            rhi::ResourceState::DepthStencilRead, m_states[slot]);
        m_states[slot] = rhi::ResourceState::DepthStencilRead;
        return h;
    }

    [[nodiscard]] u32 CascadeCount() const noexcept { return kCascadeCount; }

private:
    static constexpr rhi::TextureFormat kShadowFormat     = rhi::TextureFormat::Depth32Float;
    static constexpr u32                kShadowResolution = 1024;                  // per cascade
    static constexpr u32                kCascadeCount     = ShadowCascades::kCount;
    static constexpr u32                kMaxFramesInFlight = 8;

    // Create one frame-slot's shadow depth ARRAY (one layer per view-cascade) + an array sample view
    // for the forward. Recreated when the needed layer count grows (more views). Per-layer attachment
    // views are derived by the graph (subresource) at pass time.
    bool EnsureTexture(u32 slot, u32 layerCount) {
        if (m_textures[slot] != nullptr && m_layerCounts[slot] >= layerCount) { return true; }
        if (m_textures[slot] != nullptr) {   // grow: free the old array + its views (GPU idle at startup/resize)
            m_device->WaitIdle();
            if (m_sampleViews[slot] != nullptr) { m_device->DestroyTextureView(m_sampleViews[slot]); m_sampleViews[slot] = nullptr; }
            if (m_attachViews[slot] != nullptr) { m_device->DestroyTextureView(m_attachViews[slot]); m_attachViews[slot] = nullptr; }
            m_device->DestroyTexture(m_textures[slot]); m_textures[slot] = nullptr;
        }
        rhi::TextureDesc td{};
        td.format          = kShadowFormat;
        td.width           = kShadowResolution;
        td.height          = kShadowResolution;
        td.arrayLayerCount = layerCount;
        td.usage           = rhi::TextureUsage::DepthStencil | rhi::TextureUsage::Sampled;
        td.label           = u8"shadow.cascades";
        if (!m_device->CreateTexture(td, m_textures[slot]).IsOk()) { m_textures[slot] = nullptr; return false; }

        // Whole-array views (the import's attachment fallback + the forward's Texture2DArray sample).
        rhi::TextureViewDesc av{};
        av.format = kShadowFormat; av.aspect = rhi::TextureAspect::DepthOnly;
        av.dimension = rhi::TextureViewDimension::Texture2DArray; av.arrayLayerCount = layerCount;
        if (!m_device->CreateTextureView(m_textures[slot], av, m_attachViews[slot]).IsOk()) { return false; }
        rhi::TextureViewDesc sv{};
        sv.format = kShadowFormat; sv.aspect = rhi::TextureAspect::DepthOnly;
        sv.dimension = rhi::TextureViewDimension::Texture2DArray; sv.arrayLayerCount = layerCount;
        if (!m_device->CreateTextureView(m_textures[slot], sv, m_sampleViews[slot]).IsOk()) { return false; }
        m_layerCounts[slot] = layerCount;
        m_states[slot] = rhi::ResourceState::Undefined;
        ++m_generation;   // a new physical shadow texture exists -> invalidate consumer bind-group caches
        return true;
    }

    void Shutdown() {
        for (u32 i = 0; i < kMaxFramesInFlight; ++i) {
            if (m_sampleViews[i] != nullptr) { m_device->DestroyTextureView(m_sampleViews[i]); m_sampleViews[i] = nullptr; }
            if (m_attachViews[i] != nullptr) { m_device->DestroyTextureView(m_attachViews[i]); m_attachViews[i] = nullptr; }
            if (m_textures[i] != nullptr) { m_device->DestroyTexture(m_textures[i]); m_textures[i] = nullptr; }
        }
    }

    rhi::Device* m_device;
    u32          m_framesInFlight = 2;
    u64          m_generation = 0;   // ++ on every shadow texture (re)creation

    rhi::Texture*      m_textures[kMaxFramesInFlight]    = {};
    rhi::TextureView*  m_attachViews[kMaxFramesInFlight] = {};   // depth render target
    rhi::TextureView*  m_sampleViews[kMaxFramesInFlight] = {};   // sampled in the forward shader
    rhi::ResourceState m_states[kMaxFramesInFlight]      = {};   // last-known state (import current-state)
    u32                m_layerCounts[kMaxFramesInFlight] = {};   // current array layer count per slot
};

} // namespace raptor::render
