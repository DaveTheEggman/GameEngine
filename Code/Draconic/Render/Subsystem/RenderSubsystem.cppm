/// Draconic::RenderSubsystem - the `:subsystem` partition.
///
/// RenderSubsystem: the Context-level driver that connects scenes to the (scene-agnostic)
/// renderer. It owns the GPU systems - the DXC compiler, ShaderSystem, PipelineStateCache,
/// the MeshRenderer + RendererRegistry, and the per-frame RenderFrame driver - and, as an
/// ISceneAware, injects the mesh/camera component managers into each scene on creation.
///
/// It implements ISceneRenderer (Begin/RenderScene×N/End): the app's render callback brackets
/// the frame with BeginRendering/EndRendering and calls RenderScene per active scene. Each
/// RenderScene extracts the scene into an ExtractedScene snapshot and collects a RenderView;
/// EndRendering composes all views. (No MaterialSystem yet - the built-in forward shader binds
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
        scene.AddSystem<SpriteComponentManager>();
        scene.AddSystem<DecalComponentManager>();
        scene.AddSystem<CameraComponentManager>();
        scene.AddSystem<LightComponentManager>();
        scene.AddSystem<ReflectionProbeComponentManager>();
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

    // Ambient occlusion: mode (Off/GTAO/SSAO, mutually exclusive) + tunables (strength = composite amount,
    // radius = world AO radius, intensity = power). Both modes share the whole apply/blur/debug pipeline.
    void SetAoMode(AoMode m) noexcept { m_aoMode = m; }
    [[nodiscard]] AoMode GetAoMode() const noexcept { return m_aoMode; }
    void SetAoStrength(f32 v) noexcept { m_aoStrength = v; }
    [[nodiscard]] f32 AoStrength() const noexcept { return m_aoStrength; }
    void SetAoRadius(f32 v) noexcept { m_aoRadius = v; }
    [[nodiscard]] f32 AoRadius() const noexcept { return m_aoRadius; }
    void SetAoIntensity(f32 v) noexcept { m_aoIntensity = v; }
    [[nodiscard]] f32 AoIntensity() const noexcept { return m_aoIntensity; }
    void SetAoDebug(i32 mode) noexcept { m_aoDebug = mode; }   // 0=off, 1=AO, 2/3/4=N.xyz, 5=viewZ, 6=depth
    [[nodiscard]] i32 AoDebug() const noexcept { return m_aoDebug; }

    // Screen-space reflections: on/off + tunables (intensity, max view-space ray length, hit thickness,
    // screen-edge fade, roughness cutoff, march steps). Reflects the lit HDR before AO/TAA.
    void SetSsrEnabled(bool on) noexcept { m_ssrEnabled = on; }
    [[nodiscard]] bool SsrEnabled() const noexcept { return m_ssrEnabled; }
    [[nodiscard]] SsrPass::Params& SsrParams() noexcept { return m_ssrParams; }

    // Share instance data between the camera depth-prepass and the forward (build once). A/B toggle.
    void SetInstanceSharing(bool on) noexcept { m_instanceSharing = on; }
    [[nodiscard]] bool InstanceSharing() const noexcept { return m_instanceSharing; }

    // View-frustum culling: skip renderables outside the camera frustum per view. Off by default (a no-op
    // for benchmarks that frame everything; a win for real scenes with lots off-screen). Shadow casters are
    // sourced independently, so culling the camera view never drops a shadow.
    void SetViewCulling(bool on) noexcept { m_viewCulling = on; }
    [[nodiscard]] bool ViewCulling() const noexcept { return m_viewCulling; }
    // Last frame's cull totals (culled / considered, summed over views). 0/0 when culling was off.
    void ViewCullStats(u32& culled, u32& total) const noexcept {
        if (m_frame.Get() != nullptr) { m_frame->CullStats(culled, total); } else { culled = 0; total = 0; }
    }

    // FXAA on/off (TAA-off fallback AA - ignored while TAA is on) + sub-pixel quality (0..1).
    void SetFxaaEnabled(bool on) noexcept { m_fxaaEnabled = on; }
    [[nodiscard]] bool FxaaEnabled() const noexcept { return m_fxaaEnabled; }
    void SetFxaaSubpixel(f32 v) noexcept { m_fxaaSubpixel = v; }
    [[nodiscard]] f32 FxaaSubpixel() const noexcept { return m_fxaaSubpixel; }

    // Debug draw (immediate-mode, cleared each frame after rendering). Three destinations by WHERE the
    // draw lands: DebugGlobal() = drawn in EVERY view (world gizmos + per-view HUD, replicated per view);
    // DebugScene(scene) = only where that scene renders (no side-by-side bleed); DebugScreen() = ONCE over
    // the whole window (screen-space HUD - text/rects; 3D calls have no camera here and are ignored).
    [[nodiscard]] debug::DebugDraw& DebugGlobal() noexcept { return m_debugGlobal; }
    [[nodiscard]] debug::DebugDraw& DebugScene(scene::Scene& s) {
        if (debug::DebugDraw* p = m_debugScenes.Find(&s)) { return *p; }
        return m_debugScenes.InsertOrAssign(&s, debug::DebugDraw{});
    }
    [[nodiscard]] debug::DebugDraw& DebugScreen() noexcept { return m_debugScreen; }

    // Temporal AA on/off (projection jitter + history resolve) + resolve tunables.
    void SetTaaEnabled(bool on) noexcept { m_taaEnabled = on; }
    [[nodiscard]] bool TaaEnabled() const noexcept { return m_taaEnabled; }
    void SetTaaBlend(f32 v) noexcept { m_taaBlend = v; }
    [[nodiscard]] f32 TaaBlend() const noexcept { return m_taaBlend; }
    void SetTaaGamma(f32 v) noexcept { m_taaGamma = v; }
    [[nodiscard]] f32 TaaGamma() const noexcept { return m_taaGamma; }
    void SetTaaMotionScale(f32 v) noexcept { m_taaMotionScale = v; }
    [[nodiscard]] f32 TaaMotionScale() const noexcept { return m_taaMotionScale; }

    // Directional (CSM) shadow reach + far-fade. Both in world units: distance is the reach (clamped to
    // camera farZ); farFade is the fixed WIDTH of the soft edge over which shadows dissolve to fully-lit
    // at the boundary (kills the diagonal coverage-boundary pop on a tilted, rotating camera).
    void SetShadowDistance(f32 v) noexcept { m_shadowDistance = v; }
    [[nodiscard]] f32 ShadowDistance() const noexcept { return m_shadowDistance; }
    void SetShadowFarFade(f32 v) noexcept { m_shadowFarFade = v; }
    [[nodiscard]] f32 ShadowFarFade() const noexcept { return m_shadowFarFade; }

    // Append a per-pass GPU timing report. STALLS (waits for the GPU to finish) so the timestamps
    // are valid - intended for an on-demand dump (the P-key), not per-frame use.
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
        // single slot when the job system is absent - serial fallback).
        const u32 slotCount = HasGlobalJobSystem() ? GlobalJobs().SlotCount() : 1u;
        m_renderCtx.BeginFrame(slotCount);
        m_frame->SetExposure(m_exposure);
        m_frame->SetBloom(m_bloomEnabled ? m_bloomIntensity : 0.0f, m_bloomThreshold, m_bloomKnee);
        m_frame->SetTaa(m_taaEnabled, m_taaBlend, m_taaGamma, m_taaMotionScale);
        m_frame->SetShadowParams(m_shadowDistance, m_shadowFarFade);
        m_frame->SetAo(m_aoMode, m_aoStrength, m_aoRadius, m_aoIntensity, m_aoDebug);
        m_frame->SetFxaa(m_fxaaEnabled, m_fxaaSubpixel);
        m_frame->SetInstanceSharing(m_instanceSharing);
        m_frame->SetViewCulling(m_viewCulling);
        m_frame->SetDebug(m_debugPass.Get(), &m_debugGlobal, &m_debugScreen);
        m_frame->SetDecal(m_decalPass.Get());
        m_frame->SetSsr(m_ssrPass.Get());
        m_frame->SetSsrParams(m_ssrEnabled, m_ssrParams);
        m_frame->SetProbes(m_probeSystem.Get());
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
            if (m_spriteRenderer.Get() != nullptr) {           // billboards (into the same snapshot, after meshes)
                ExtractSpritesInto(scene, *snapshot, m_spriteRenderer->RendererId());
            }
            ExtractDecalsInto(scene, *snapshot);               // screen-space decals (DecalPass, not the Renderer path)
            ExtractLightsInto(scene, *snapshot);               // lights are shading inputs, not draws
            ExtractReflectionProbesInto(scene, *snapshot);     // reflection probes (capture/prefilter inputs)
            ExtractEnvironmentInto(scene, *snapshot);          // per-scene ambient
            if (m_probeSystem.Get() != nullptr) {              // map probes to persistent array slots (capture in P1b)
                m_probeSystem->Assign(snapshot->ReflectionProbes());
            }
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
            const void* sceneDebug = m_debugScenes.Find(&scene);   // this scene's per-scene gizmo list (or null)
            m_frame->AddView(*snapshot, camera, settings, target, targetFormat, width, height, sceneDebug);
        }
    }

    void EndRendering() override {
        DRACONIC_PROFILE_SCOPE("Render.Compose");
        if (m_frame.Get() != nullptr) { m_frame->End(); }
        // Immediate-mode: clear all debug lists AFTER rendering, so next frame's draws start empty
        // (the app accumulates during its update, before the next BeginRendering).
        m_debugGlobal.Clear();
        m_debugScreen.Clear();
        for (auto& kv : m_debugScenes) { kv.value.Clear(); }
    }

protected:
    void OnInit() override {
        if (!shaders::createCompiler(shaders::CompilerDesc{}, m_compiler).IsOk() || m_compiler == nullptr) {
            return;   // no shader compiler - renderer stays inert
        }
        m_shaders  = MakeUnique<shaders::ShaderSystem>(DefaultAllocator(), *m_compiler, *m_device);
        m_psoCache = MakeUnique<materials::PipelineStateCache>(DefaultAllocator(), *m_shaders, *m_device);
        m_materialSystem = MakeUnique<materials::MaterialSystem>(DefaultAllocator());
        if (!m_materialSystem->Initialize(*m_device).IsOk()) { m_materialSystem.Reset(); return; }

        m_meshRenderer = MakeUnique<MeshRenderer>(DefaultAllocator(), *m_device, *m_shaders, *m_psoCache, *m_materialSystem, m_framesInFlight);
        if (!m_meshRenderer->Initialize().IsOk()) { m_meshRenderer.Reset(); return; }
        m_registry.Register(m_meshRenderer.Get());   // FIRST -> renderer id 0 (the RenderData default)

        // Sprites: registered after the mesh renderer (id 1); shares the blended forward pass.
        m_spriteRenderer = MakeUnique<SpriteRenderer>(DefaultAllocator(), *m_device, *m_shaders, m_framesInFlight);
        if (m_spriteRenderer->Initialize().IsOk()) { m_registry.Register(m_spriteRenderer.Get()); }
        else { m_spriteRenderer.Reset(); }

        // Debug toggles: flip to false to isolate a subsystem (e.g. bisecting a rendering bug). When
        // off, the renderer falls back gracefully - clustering off => the shader's all-lights path;
        // shadows off => unshadowed. Kept as compile-time flags (zero cost when on).
        constexpr bool kEnableClusters = true;
        constexpr bool kEnableShadows  = true;

        // Clustered light culling: a build compute pass per view (declared into the frame graph by
        // RenderFrame). Optional - if it fails to init, the renderer runs without clustering.
        if (kEnableClusters) {
            m_clusterSystem = MakeUnique<ClusterSystem>(DefaultAllocator(), *m_device, *m_shaders, m_framesInFlight);
            if (!m_clusterSystem->Initialize().IsOk()) { m_clusterSystem.Reset(); }
        }

        // HDR resolve: forward renders linear HDR, this pass tonemaps to the LDR target. Optional -
        // if it fails to init, the renderer falls back to writing the LDR target directly.
        m_tonemapPass = MakeUnique<TonemapPass>(DefaultAllocator(), *m_device, *m_shaders, m_framesInFlight);
        if (!m_tonemapPass->Initialize().IsOk()) { m_tonemapPass.Reset(); }

        // Directional shadow map (phase 5). Optional - if it fails to init, the scene renders unshadowed.
        if (kEnableShadows) {
            m_shadowSystem = MakeUnique<ShadowSystem>(DefaultAllocator(), *m_device, m_framesInFlight);
            if (!m_shadowSystem->Initialize().IsOk()) { m_shadowSystem.Reset(); }
        }

        // Image-based lighting (phase 6). Optional - if it fails to init, the scene uses flat ambient.
        m_iblSystem = MakeUnique<IBLSystem>(DefaultAllocator(), *m_device, *m_shaders);
        if (!m_iblSystem->Initialize().IsOk()) { m_iblSystem.Reset(); }

        m_probeSystem = MakeUnique<ReflectionProbeSystem>(DefaultAllocator(), *m_device, *m_shaders);
        if (!m_probeSystem->Initialize().IsOk()) { m_probeSystem.Reset(); }

        // Visible sky (background) from the IBL environment. Optional.
        m_skyPass = MakeUnique<SkyPass>(DefaultAllocator(), *m_device, *m_shaders, m_framesInFlight);
        if (!m_skyPass->Initialize().IsOk()) { m_skyPass.Reset(); }

        // HDR bloom (composited at tonemap). Optional.
        m_bloomPass = MakeUnique<BloomPass>(DefaultAllocator(), *m_device, *m_shaders);
        if (!m_bloomPass->Initialize().IsOk()) { m_bloomPass.Reset(); }

        // Temporal AA resolve (per-view history). Optional.
        m_taaPass = MakeUnique<TaaPass>(DefaultAllocator(), *m_device, *m_shaders);
        if (!m_taaPass->Initialize().IsOk()) { m_taaPass.Reset(); }

        // Ambient occlusion (GTAO/SSAO from the G-buffer). Optional.
        m_aoPass = MakeUnique<AoPass>(DefaultAllocator(), *m_device, *m_shaders);
        if (!m_aoPass->Initialize().IsOk()) { m_aoPass.Reset(); }

        // Screen-space reflections (reflect the lit HDR before AO/TAA). Optional.
        m_ssrPass = MakeUnique<SsrPass>(DefaultAllocator(), *m_device, *m_shaders);
        if (!m_ssrPass->Initialize().IsOk()) { m_ssrPass.Reset(); }

        // FXAA (TAA-off fallback AA). Optional.
        m_fxaaPass = MakeUnique<FxaaPass>(DefaultAllocator(), *m_device, *m_shaders, m_framesInFlight);
        if (!m_fxaaPass->Initialize().IsOk()) { m_fxaaPass.Reset(); }

        // Screen-space decals (project onto depth, blend into HDR before AO/TAA). Optional.
        m_decalPass = MakeUnique<DecalPass>(DefaultAllocator(), *m_device, *m_shaders, m_framesInFlight);
        if (!m_decalPass->Initialize().IsOk()) { m_decalPass.Reset(); }

        // Debug draw (per-view gizmos + screen text). Optional.
        m_debugPass = MakeUnique<DebugDrawPass>(DefaultAllocator(), *m_device, *m_shaders, m_framesInFlight);
        if (!m_debugPass->Initialize().IsOk()) { m_debugPass.Reset(); }

        m_frame = MakeUnique<RenderFrame>(DefaultAllocator(), *m_device, m_registry, m_framesInFlight,
                                          m_clusterSystem.Get(), m_tonemapPass.Get(), m_shadowSystem.Get(),
                                          m_iblSystem.Get(), m_skyPass.Get(), m_bloomPass.Get(), m_taaPass.Get(), m_aoPass.Get(),
                                          m_fxaaPass.Get());
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
        m_taaPass.Reset();      // before the ShaderSystem it borrows
        m_aoPass.Reset();       // before the ShaderSystem it borrows
        m_ssrPass.Reset();      // before the ShaderSystem it borrows
        m_fxaaPass.Reset();     // before the ShaderSystem it borrows
        m_decalPass.Reset();    // before the ShaderSystem it borrows
        m_debugPass.Reset();    // before the ShaderSystem it borrows
        m_probeSystem.Reset();  // probe textures/buffers (before the ShaderSystem it borrows)
        m_iblSystem.Reset();    // IBL textures/buffers (before the ShaderSystem it borrows)
        m_spriteRenderer.Reset(); // before the ShaderSystem it borrows
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
    UniquePtr<SpriteRenderer>                 m_spriteRenderer;
    UniquePtr<ClusterSystem>                  m_clusterSystem;
    UniquePtr<TonemapPass>                    m_tonemapPass;
    UniquePtr<ShadowSystem>                   m_shadowSystem;
    UniquePtr<IBLSystem>                      m_iblSystem;
    UniquePtr<ReflectionProbeSystem>         m_probeSystem;
    UniquePtr<SkyPass>                        m_skyPass;
    UniquePtr<BloomPass>                      m_bloomPass;
    UniquePtr<TaaPass>                        m_taaPass;
    UniquePtr<AoPass>                         m_aoPass;
    UniquePtr<SsrPass>                         m_ssrPass;
    UniquePtr<FxaaPass>                        m_fxaaPass;
    UniquePtr<DecalPass>                       m_decalPass;
    UniquePtr<DebugDrawPass>                   m_debugPass;
    debug::DebugDraw                           m_debugGlobal;                 // global gizmos (all views)
    debug::DebugDraw                           m_debugScreen;                 // whole-window HUD (drawn once)
    HashMap<scene::Scene*, debug::DebugDraw>   m_debugScenes;                 // per-scene gizmos
    f32                                       m_exposure = 1.0f;
    bool                                      m_bloomEnabled   = true;
    AoMode                                    m_aoMode         = AoMode::Off;   // AO off by default (UI combo)
    i32                                       m_aoDebug        = 0;             // AO debug view (0=off)
    f32                                       m_aoStrength     = 0.6f;          // partial by default (full darkens curved surfaces too much)
    f32                                       m_aoRadius       = 0.5f;
    f32                                       m_aoIntensity    = 1.0f;
    bool                                      m_ssrEnabled     = false;   // SSR off by default (UI toggle)
    SsrPass::Params                           m_ssrParams{};
    bool                                      m_instanceSharing = true;   // prepass->forward instance-data sharing (A/B toggle)
    bool                                      m_viewCulling     = false;  // view-frustum cull camera draw lists (default off)
    bool                                      m_fxaaEnabled    = false;   // FXAA off by default (TAA-off fallback)
    f32                                       m_fxaaSubpixel   = 0.75f;
    bool                                      m_taaEnabled     = false;   // TAA off by default (UI toggle)
    f32                                       m_taaBlend       = 0.97f;   // history weight (stability)
    f32                                       m_taaGamma       = 1.25f;   // variance-clip box half-width
    f32                                       m_taaMotionScale = 32.0f;   // history drop-off with motion
    f32                                       m_shadowDistance = 300.0f;  // directional-shadow reach (world units)
    f32                                       m_shadowFarFade  = 40.0f;   // far-fade width (world units)
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
