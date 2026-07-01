/// Draconic::RenderSubsystem — the `:subsystem` partition.
///
/// RenderSubsystem: the Context-level driver that connects scenes to the (scene-agnostic)
/// renderer. It owns the GPU systems — the DXC compiler, ShaderSystem, PipelineStateCache,
/// the MeshRenderer + RendererRegistry, and the per-frame RenderFrame driver — and, as an
/// ISceneAware, injects the mesh/camera component managers into each scene on creation.
///
/// It implements ISceneRenderer (Begin/RenderScene×N/End): the app's render callback brackets
/// the frame with BeginRendering/EndRendering and calls RenderScene per active scene. Each
/// RenderScene extracts the scene into an ExtractedScene snapshot and collects a RenderView;
/// EndRendering composes all views. (No MaterialSystem yet — the built-in forward shader binds
/// no material set; that lands with material binding in phase 3.)

module;
#include "Core/Prelude.h"
#include "Profiler/Profiler.h"

export module draconic.render.subsystem:subsystem;

import draconic.core;
import draconic.rhi;
import draconic.profiler;
import draconic.runtime;            // Subsystem, Context
import draconic.scene;              // Scene, ISceneAware
import draconic.scene.subsystem;    // SceneSubsystem (to register as scene-aware)
import draconic.shaders;            // Compiler
import draconic.shaders.system;     // ShaderSystem
import draconic.materials;          // MaterialSystem
import draconic.materials.pso;      // PipelineStateCache
import draconic.render;             // MeshRenderer, RendererRegistry, RenderFrame, ExtractedScene
import :components;
import :extract;
import :scene_renderer;

using namespace draconic::core;
namespace rhi = draconic::rhi;

export namespace draconic::render {

class RenderSubsystem final : public draconic::runtime::Subsystem,
                              public ISceneRenderer,
                              public scene::ISceneAware {
public:
    RenderSubsystem(rhi::Device& device, u32 framesInFlight) noexcept
        : m_device(&device), m_framesInFlight(framesInFlight < 1 ? 1 : framesInFlight) {}

    [[nodiscard]] i32 UpdateOrder() const noexcept override { return 1000; }   // late (renders, doesn't tick)

    // Injects the render component managers into each new scene.
    void OnSceneCreated(scene::Scene& scene) override {
        scene.AddSystem<MeshComponentManager>();
        scene.AddSystem<CameraComponentManager>();
        scene.AddSystem<LightComponentManager>();
        scene.AddSystem<EnvironmentSystem>();
    }

    [[nodiscard]] bool IsReady() const noexcept { return m_frame.Get() != nullptr; }

    // Set the scene's HDR equirectangular environment (RGBA32F, w*h*4 floats). The IBL env rebuilds
    // from it when EnvironmentSettings.skyMode is HDREquirect. Owns a copy of the pixels (uploads next
    // frame). No-op if IBL is unavailable.
    void SetSkyEquirect(u32 w, u32 h, Span<const f32> rgba) {
        if (m_iblSystem.Get() != nullptr) { m_iblSystem->SetEquirect(w, h, rgba); }
    }

    // Set the scene's cubemap environment: 6 RGBA8 faces (+X,-X,+Y,-Y,+Z,-Z) concatenated, each
    // faceSize*faceSize*4 bytes. Used when EnvironmentSettings.skyMode is Cubemap.
    void SetSkyCubemap(u32 faceSize, Span<const u8> sixFaces) {
        if (m_iblSystem.Get() != nullptr) { m_iblSystem->SetCubemap(faceSize, sixFaces); }
    }

    // Linear exposure multiplier applied in the tonemap (default 1.0).
    void SetExposure(f32 exposure) noexcept { m_exposure = exposure; }
    [[nodiscard]] f32 Exposure() const noexcept { return m_exposure; }

    // Bloom on/off (skips the whole pyramid when off) + composite strength + soft-knee prefilter.
    void SetBloomEnabled(bool on) noexcept { m_bloomEnabled = on; }
    [[nodiscard]] bool BloomEnabled() const noexcept { return m_bloomEnabled; }
    void SetBloomIntensity(f32 v) noexcept { m_bloomIntensity = v; }
    [[nodiscard]] f32 BloomIntensity() const noexcept { return m_bloomIntensity; }
    void SetBloomThreshold(f32 v) noexcept { m_bloomThreshold = v; }
    [[nodiscard]] f32 BloomThreshold() const noexcept { return m_bloomThreshold; }

    // Append a per-pass GPU timing report. STALLS (waits for the GPU to finish) so the timestamps
    // are valid — intended for an on-demand dump (the P-key), not per-frame use.
    void BuildGpuProfileReport(String& out) {
        if (m_frame.Get() == nullptr || m_device == nullptr) { return; }
        m_device->WaitIdle();
        m_frame->ReadGpuProfile(out);
    }

    // ---- ISceneRenderer ----

    void BeginRendering(rhi::CommandEncoder& encoder, u32 frameIndex) override {
        DRACONIC_PROFILE_SCOPE("Render.Begin");
        if (m_frame.Get() == nullptr) { return; }
        m_sceneCount = 0;
        // Provision per-worker extraction arenas for this frame (one per job-system slot, or a
        // single slot when the job system is absent — serial fallback).
        const u32 slotCount = HasGlobalJobSystem() ? GlobalJobs().SlotCount() : 1u;
        m_renderCtx.BeginFrame(slotCount);
        m_frame->SetExposure(m_exposure);
        m_frame->SetBloom(m_bloomEnabled ? m_bloomIntensity : 0.0f, m_bloomThreshold, m_bloomKnee);
        m_frame->Begin(encoder, frameIndex);
    }

    void RenderScene(scene::Scene& scene, rhi::TextureView* target, rhi::TextureFormat targetFormat,
                     u32 width, u32 height, ViewportRect viewport = {},
                     const CameraOverride* cameraOverride = nullptr,
                     const TargetState& targetState = {}) override {
        if (m_frame.Get() == nullptr || target == nullptr) { return; }

        ExtractedScene* snapshot = AcquireScene();
        {
            DRACONIC_PROFILE_SCOPE("Render.Extract");
            ExtractSceneInto(scene, *snapshot, m_renderCtx);   // parallel when the job system is up (resets snapshot)
            ExtractLightsInto(scene, *snapshot);               // lights are shading inputs, not draws
            ExtractEnvironmentInto(scene, *snapshot);          // per-scene ambient
        }

        ViewCamera camera;
        Color clearColor{ 0.392f, 0.584f, 0.929f, 1.0f };     // cornflower fallback (no primary camera)
        if (cameraOverride != nullptr) { camera = cameraOverride->camera; clearColor = cameraOverride->clearColor; }
        else { (void)ExtractPrimaryCamera(scene, camera, &clearColor); }   // clear comes from the camera

        ViewSettings settings;
        settings.clear = rhi::ClearColor{ clearColor.r, clearColor.g, clearColor.b, clearColor.a };
        settings.viewportX = viewport.x; settings.viewportY = viewport.y;
        settings.viewportWidth = viewport.width; settings.viewportHeight = viewport.height;
        settings.targetTexture = targetState.texture;
        settings.targetCurrentState = targetState.currentState;
        settings.targetFinalState = targetState.finalState;
        {
            DRACONIC_PROFILE_SCOPE("Render.AddView");   // binds the view + builds/sorts its draw list
            m_frame->AddView(*snapshot, camera, settings, target, targetFormat, width, height);
        }
    }

    void EndRendering() override {
        DRACONIC_PROFILE_SCOPE("Render.Compose");
        if (m_frame.Get() != nullptr) { m_frame->End(); }
    }

protected:
    void OnInit() override {
        if (!shaders::createCompiler(shaders::CompilerDesc{}, m_compiler).IsOk() || m_compiler == nullptr) {
            return;   // no shader compiler — renderer stays inert
        }
        m_shaders  = MakeUnique<shaders::ShaderSystem>(DefaultAllocator(), *m_compiler, *m_device);
        m_psoCache = MakeUnique<materials::PipelineStateCache>(DefaultAllocator(), *m_shaders, *m_device);
        m_materialSystem = MakeUnique<materials::MaterialSystem>(DefaultAllocator());
        if (!m_materialSystem->Initialize(*m_device).IsOk()) { m_materialSystem.Reset(); return; }

        m_meshRenderer = MakeUnique<MeshRenderer>(DefaultAllocator(), *m_device, *m_shaders, *m_psoCache, *m_materialSystem, m_framesInFlight);
        if (!m_meshRenderer->Initialize().IsOk()) { m_meshRenderer.Reset(); return; }
        m_registry.Register(m_meshRenderer.Get());

        // Debug toggles: flip to false to isolate a subsystem (e.g. bisecting a rendering bug). When
        // off, the renderer falls back gracefully — clustering off => the shader's all-lights path;
        // shadows off => unshadowed. Kept as compile-time flags (zero cost when on).
        constexpr bool kEnableClusters = true;
        constexpr bool kEnableShadows  = true;

        // Clustered light culling: a build compute pass per view (declared into the frame graph by
        // RenderFrame). Optional — if it fails to init, the renderer runs without clustering.
        if (kEnableClusters) {
            m_clusterSystem = MakeUnique<ClusterSystem>(DefaultAllocator(), *m_device, *m_shaders, m_framesInFlight);
            if (!m_clusterSystem->Initialize().IsOk()) { m_clusterSystem.Reset(); }
        }

        // HDR resolve: forward renders linear HDR, this pass tonemaps to the LDR target. Optional —
        // if it fails to init, the renderer falls back to writing the LDR target directly.
        m_tonemapPass = MakeUnique<TonemapPass>(DefaultAllocator(), *m_device, *m_shaders, m_framesInFlight);
        if (!m_tonemapPass->Initialize().IsOk()) { m_tonemapPass.Reset(); }

        // Directional shadow map (phase 5). Optional — if it fails to init, the scene renders unshadowed.
        if (kEnableShadows) {
            m_shadowSystem = MakeUnique<ShadowSystem>(DefaultAllocator(), *m_device, m_framesInFlight);
            if (!m_shadowSystem->Initialize().IsOk()) { m_shadowSystem.Reset(); }
        }

        // Image-based lighting (phase 6). Optional — if it fails to init, the scene uses flat ambient.
        m_iblSystem = MakeUnique<IBLSystem>(DefaultAllocator(), *m_device, *m_shaders);
        if (!m_iblSystem->Initialize().IsOk()) { m_iblSystem.Reset(); }

        // Visible sky (background) from the IBL environment. Optional.
        m_skyPass = MakeUnique<SkyPass>(DefaultAllocator(), *m_device, *m_shaders, m_framesInFlight);
        if (!m_skyPass->Initialize().IsOk()) { m_skyPass.Reset(); }

        // HDR bloom (composited at tonemap). Optional.
        m_bloomPass = MakeUnique<BloomPass>(DefaultAllocator(), *m_device, *m_shaders);
        if (!m_bloomPass->Initialize().IsOk()) { m_bloomPass.Reset(); }

        m_frame = MakeUnique<RenderFrame>(DefaultAllocator(), *m_device, m_registry, m_framesInFlight,
                                          m_clusterSystem.Get(), m_tonemapPass.Get(), m_shadowSystem.Get(),
                                          m_iblSystem.Get(), m_skyPass.Get(), m_bloomPass.Get());
        m_frame->EnableGpuProfiling();   // per-pass GPU timestamps (cheap; read on the P-key dump)
    }

    void OnReady() override {
        // Register as scene-aware so we inject our managers into scenes the app creates.
        if (draconic::runtime::Context* ctx = GetContext()) {
            if (auto* scenes = ctx->GetSubsystem<scene::SceneSubsystem>()) { scenes->RegisterSceneAware(this); }
        }
    }

    void OnShutdown() override {
        if (draconic::runtime::Context* ctx = GetContext()) {
            if (auto* scenes = ctx->GetSubsystem<scene::SceneSubsystem>()) { scenes->UnregisterSceneAware(this); }
        }
        m_device->WaitIdle();   // GPU must finish before we free its buffers/PSOs/descriptors
        m_frame.Reset();        // releases the forward pass's per-frame GPU resources
        m_clusterSystem.Reset();// before the ShaderSystem it borrows
        m_tonemapPass.Reset();  // before the ShaderSystem it borrows
        m_shadowSystem.Reset(); // shadow depth textures
        m_skyPass.Reset();      // before the ShaderSystem it borrows
        m_bloomPass.Reset();    // before the ShaderSystem it borrows
        m_iblSystem.Reset();    // IBL textures/buffers (before the ShaderSystem it borrows)
        m_meshRenderer.Reset(); // before the systems it borrows (releases material instances first)
        m_materialSystem.Reset();
        m_psoCache.Reset();
        m_shaders.Reset();
        if (m_compiler != nullptr) { m_compiler->Destroy(); m_compiler = nullptr; }
    }

private:
    // A per-frame snapshot pool: one ExtractedScene per RenderScene call, kept alive (and its
    // arena chunks reused) until the next BeginRendering. (Phase 8 shares one snapshot across
    // multiple cameras of the same scene; phase 1 takes one per call.)
    [[nodiscard]] ExtractedScene* AcquireScene() {
        if (m_sceneCount == m_scenes.Size()) {
            m_scenes.PushBack(MakeUnique<ExtractedScene>(DefaultAllocator()));
        }
        ExtractedScene* s = m_scenes[m_sceneCount++].Get();
        s->Reset();
        return s;
    }

    rhi::Device*       m_device;
    u32                m_framesInFlight = 2;
    shaders::Compiler* m_compiler = nullptr;
    UniquePtr<shaders::ShaderSystem>          m_shaders;
    UniquePtr<materials::PipelineStateCache>  m_psoCache;
    UniquePtr<materials::MaterialSystem>      m_materialSystem;
    UniquePtr<MeshRenderer>                   m_meshRenderer;
    UniquePtr<ClusterSystem>                  m_clusterSystem;
    UniquePtr<TonemapPass>                    m_tonemapPass;
    UniquePtr<ShadowSystem>                   m_shadowSystem;
    UniquePtr<IBLSystem>                      m_iblSystem;
    UniquePtr<SkyPass>                        m_skyPass;
    UniquePtr<BloomPass>                      m_bloomPass;
    f32                                       m_exposure = 1.0f;
    bool                                      m_bloomEnabled   = true;
    f32                                       m_bloomIntensity = 0.05f;   // 0 = bloom off
    f32                                       m_bloomThreshold = 1.0f;
    f32                                       m_bloomKnee      = 0.6f;
    RendererRegistry                          m_registry;
    UniquePtr<RenderFrame>                    m_frame;

    Array<UniquePtr<ExtractedScene>>          m_scenes;       // snapshot pool
    usize                                     m_sceneCount = 0;
    RenderContext                             m_renderCtx;    // per-worker extraction arenas
};

} // namespace draconic::render
