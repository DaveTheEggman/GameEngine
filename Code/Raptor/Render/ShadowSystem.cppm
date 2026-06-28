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

using namespace raptor::core;
namespace rhi = raptor::rhi;

export namespace raptor::render {

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

    // Ensure this frame's shadow texture exists (called before the mesh renderer builds set 0, so the
    // map view is available to bind). Returns the sample view, or null on failure.
    rhi::TextureView* PrepareFrame(u32 frameIndex) {
        const u32 slot = frameIndex % m_framesInFlight;
        return EnsureTexture(slot) ? m_sampleViews[slot] : nullptr;
    }

    [[nodiscard]] rhi::TextureView* SampleView(u32 frameIndex) const noexcept {
        return m_sampleViews[frameIndex % m_framesInFlight];
    }

    // Import this frame's shadow texture into the graph: the depth pass writes it, then it barriers to
    // DepthStencilRead so the forward pass samples it. Returns the graph handle (invalid if no texture).
    rendergraph::RGHandle ImportTarget(rendergraph::RenderGraph& graph, u32 frameIndex) {
        const u32 slot = frameIndex % m_framesInFlight;
        if (!EnsureTexture(slot)) { return {}; }
        const rendergraph::RGHandle h = graph.ImportTarget(
            u8"shadow.map", m_textures[slot], m_attachViews[slot], m_sampleViews[slot],
            rhi::ResourceState::DepthStencilRead, m_states[slot]);
        m_states[slot] = rhi::ResourceState::DepthStencilRead;
        return h;
    }

private:
    static constexpr rhi::TextureFormat kShadowFormat     = rhi::TextureFormat::Depth32Float;
    static constexpr u32                kShadowResolution = 2048;
    static constexpr u32                kMaxFramesInFlight = 8;

    // Create one frame-slot's shadow depth texture + its attachment/sample views (lazy, once).
    bool EnsureTexture(u32 slot) {
        if (m_textures[slot] != nullptr) { return true; }
        rhi::TextureDesc td{};
        td.format = kShadowFormat;
        td.width  = kShadowResolution;
        td.height = kShadowResolution;
        td.usage  = rhi::TextureUsage::DepthStencil | rhi::TextureUsage::Sampled;
        td.label  = u8"shadow.map";
        if (!m_device->CreateTexture(td, m_textures[slot]).IsOk()) { m_textures[slot] = nullptr; return false; }

        rhi::TextureViewDesc attach{}; attach.format = kShadowFormat; attach.aspect = rhi::TextureAspect::DepthOnly;
        if (!m_device->CreateTextureView(m_textures[slot], attach, m_attachViews[slot]).IsOk()) { return false; }
        rhi::TextureViewDesc sample{}; sample.format = kShadowFormat; sample.aspect = rhi::TextureAspect::DepthOnly;
        if (!m_device->CreateTextureView(m_textures[slot], sample, m_sampleViews[slot]).IsOk()) { return false; }
        m_states[slot] = rhi::ResourceState::Undefined;
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

    rhi::Texture*      m_textures[kMaxFramesInFlight]    = {};
    rhi::TextureView*  m_attachViews[kMaxFramesInFlight] = {};   // depth render target
    rhi::TextureView*  m_sampleViews[kMaxFramesInFlight] = {};   // sampled in the forward shader
    rhi::ResourceState m_states[kMaxFramesInFlight]      = {};   // last-known state (import current-state)
};

} // namespace raptor::render
