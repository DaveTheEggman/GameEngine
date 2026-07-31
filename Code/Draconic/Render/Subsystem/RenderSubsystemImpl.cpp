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
#include "Draconic.Core/Prelude.h"
#include "Draconic.Profiler/Profiler.h"

module draconic.render.subsystem;

import draconic.core;
import draconic.rhi;
import draconic.profiler;
import draconic.runtime;         // Subsystem, Context
import draconic.scene;           // Scene, ISceneAware
import draconic.scene.subsystem; // SceneSubsystem (to register as scene-aware)
import draconic.shaders.system;  // ShaderSystem (borrowed from the host)
import draconic.materials;       // MaterialSystem
import draconic.materials.pso;   // PipelineStateCache
import draconic.render;          // MeshRenderer, RendererRegistry, RenderFrame, ExtractedScene
import :components;
import :extract;
import :scene_renderer;

using namespace draconic::core;
namespace rhi = draconic::rhi;

namespace draconic::render
{
    u16 RenderSubsystem::RegisterRenderer(Renderer& renderer)
    {
        m_registry.Register(&renderer);
        return renderer.RendererId();
    }

    void RenderSubsystem::RegisterProvider(scene::Scene& scene, IRenderDataProvider& provider)
    {
        m_providers.PushBack(SceneProvider{&scene, &provider});
    }

    i32 RenderSubsystem::UpdateOrder() const noexcept
    {
        return 1000;
    } // late (renders, doesn't tick)

    void RenderSubsystem::OnSceneCreated(scene::Scene& scene)
    {
        scene.AddSystem<MeshComponentManager>();
        scene.AddSystem<InstancedMeshComponentManager>();
        scene.AddSystem<SpriteComponentManager>();
        scene.AddSystem<DecalComponentManager>();
        scene.AddSystem<CameraComponentManager>();
        scene.AddSystem<LightComponentManager>();
        scene.AddSystem<ReflectionProbeComponentManager>();
        scene.AddSystem<EnvironmentSystem>();
        scene.AddSystem<PostProcessSystem>();
    }

    void RenderSubsystem::OnSceneDestroyed(scene::Scene& scene)
    {
        usize w = 0;
        for (usize r = 0; r < m_providers.Size(); ++r)
        {
            if (m_providers[r].scene != &scene)
            {
                m_providers[w++] = m_providers[r];
            }
        }
        m_providers.Resize(w);
    }

    void RenderSubsystem::SetSkyEquirect(u32 w, u32 h, Span<const f32> rgba)
    {
        if (m_iblSystem.Get() != nullptr)
        {
            m_iblSystem->SetEquirect(w, h, rgba);
        }
    }

    void RenderSubsystem::SetSkyCubemap(u32 faceSize, Span<const u8> sixFaces)
    {
        if (m_iblSystem.Get() != nullptr)
        {
            m_iblSystem->SetCubemap(faceSize, sixFaces);
        }
    }

    void RenderSubsystem::SetExposure(f32 exposure) noexcept
    {
        m_exposure = exposure;
        m_globalPostActive = true;
    }

    void RenderSubsystem::SetBloomEnabled(bool on) noexcept
    {
        m_bloomEnabled = on;
        m_globalPostActive = true;
    }

    void RenderSubsystem::SetBloomIntensity(f32 v) noexcept
    {
        m_bloomIntensity = v;
        m_globalPostActive = true;
    }

    void RenderSubsystem::SetBloomThreshold(f32 v) noexcept
    {
        m_bloomThreshold = v;
        m_globalPostActive = true;
    }

    void RenderSubsystem::SetAoMode(AoMode m) noexcept
    {
        m_aoMode = m;
        m_globalPostActive = true;
    }

    void RenderSubsystem::SetAoStrength(f32 v) noexcept
    {
        m_aoStrength = v;
        m_globalPostActive = true;
    }

    void RenderSubsystem::SetAoRadius(f32 v) noexcept
    {
        m_aoRadius = v;
        m_globalPostActive = true;
    }

    void RenderSubsystem::SetAoIntensity(f32 v) noexcept
    {
        m_aoIntensity = v;
        m_globalPostActive = true;
    }

    void RenderSubsystem::SetAoDebug(i32 mode) noexcept
    {
        m_aoDebug = mode;
    } // 0=off, 1=AO, 2/3/4=N.xyz, 5=viewZ, 6=depth

    void RenderSubsystem::SetSsrEnabled(bool on) noexcept
    {
        m_ssrEnabled = on;
        m_globalPostActive = true;
    }

    void RenderSubsystem::ViewCullStats(u32& culled, u32& total) const noexcept
    {
        if (m_frame.Get() != nullptr)
        {
            m_frame->CullStats(culled, total);
        }
        else
        {
            culled = 0;
            total = 0;
        }
    }

    void RenderSubsystem::SetFxaaEnabled(bool on) noexcept
    {
        m_fxaaEnabled = on;
        m_globalPostActive = true;
    }

    void RenderSubsystem::SetFxaaSubpixel(f32 v) noexcept
    {
        m_fxaaSubpixel = v;
        m_globalPostActive = true;
    }

    debug::DebugDraw& RenderSubsystem::DebugScene(scene::Scene& s)
    {
        if (debug::DebugDraw* p = m_debugScenes.Find(&s))
        {
            return *p;
        }
        return m_debugScenes.InsertOrAssign(&s, debug::DebugDraw{});
    }

    void RenderSubsystem::SetTaaEnabled(bool on) noexcept
    {
        m_taaEnabled = on;
        m_globalPostActive = true;
    }

    void RenderSubsystem::SetTaaBlend(f32 v) noexcept
    {
        m_taaBlend = v;
        m_globalPostActive = true;
    }

    void RenderSubsystem::SetTaaGamma(f32 v) noexcept
    {
        m_taaGamma = v;
        m_globalPostActive = true;
    }

    void RenderSubsystem::BuildGpuProfileReport(String& out)
    {
        if (m_frame.Get() == nullptr || m_device == nullptr)
        {
            return;
        }
        m_device->WaitIdle();
        m_frame->ReadGpuProfile(out);
    }

    void RenderSubsystem::BeginRendering(rhi::CommandEncoder& encoder, u32 frameIndex)
    {
        DRACONIC_PROFILE_SCOPE("Render.Begin");
        if (m_frame.Get() == nullptr)
        {
            return;
        }
        // Dev hot reload (throttled inside the provider). On a reload, idle the GPU so
        // passes can destroy + rebuild their version-stamped pipelines immediately (a
        // dev-only hiccup; the material path still goes through the PSO retire ring).
        if (m_shaders != nullptr && m_shaders->PumpReloads() > 0)
        {
            m_device->WaitIdle();
        }
        m_sceneCount = 0;
        m_snapshotOwners.Resize(m_scenes.Size()); // per-frame scene tags for snapshot sharing
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
        m_frame->SetSceneOverlays(&m_sceneOverlays.Items());
        m_frame->SetDecal(m_decalPass.Get());
        m_frame->SetSsr(m_ssrPass.Get());
        m_frame->SetSsrParams(m_ssrEnabled, m_ssrParams);
        m_frame->SetProbes(m_probeSystem.Get());
        if (m_probeSystem.Get() != nullptr)
        {
            m_probeSystem->BeginFrame();
        } // records re-accumulate per scene
        m_frame->Begin(encoder, frameIndex);
    }

    void RenderSubsystem::RenderScene(scene::Scene& scene, rhi::TextureView* target,
                                      rhi::TextureFormat targetFormat, u32 width, u32 height,
                                      ViewportRect viewport, const CameraOverride* cameraOverride,
                                      const TargetState& targetState,
                                      const ViewPostOverride* postOverride)
    {
        if (m_frame.Get() == nullptr || target == nullptr)
        {
            return;
        }

        // ONE extraction per scene per frame: a scene rendered through several views
        // (split-screen) shares one snapshot - so the per-scene shadow/probe/IBL work
        // grouped on the snapshot downstream runs once, not per view. (Scenes must not
        // mutate between RenderScene calls of one frame - the Begin/End bracket contract.)
        ExtractedScene* snapshot = nullptr;
        for (usize i = 0; i < m_sceneCount; ++i)
        {
            if (m_snapshotOwners[i] == &scene)
            {
                snapshot = m_scenes[i].Get();
                break;
            }
        }
        const bool firstSight = (snapshot == nullptr);
        if (firstSight)
        {
            snapshot = AcquireScene();
            if (m_snapshotOwners.Size() < m_sceneCount)
            {
                m_snapshotOwners.Resize(m_sceneCount);
            }
            m_snapshotOwners[m_sceneCount - 1] = &scene;
        }
        if (firstSight)
        {
            DRACONIC_PROFILE_SCOPE("Render.Extract");
            ExtractSceneInto(scene, *snapshot,
                             m_renderCtx); // parallel when the job system is up (resets snapshot)
            ExtractInstancedMeshesInto(
                scene, *snapshot); // instanced sets (MultiMesh): one item each, O(1)/frame
            if (m_spriteRenderer.Get() != nullptr)
            { // billboards (into the same snapshot, after meshes)
                ExtractSpritesInto(scene, *snapshot, m_spriteRenderer->RendererId());
            }
            ExtractDecalsInto(scene,
                              *snapshot); // screen-space decals (DecalPass, not the Renderer path)
            ExtractLightsInto(scene, *snapshot); // lights are shading inputs, not draws
            ExtractReflectionProbesInto(scene,
                                        *snapshot); // reflection probes (capture/prefilter inputs)
            ExtractEnvironmentInto(scene, *snapshot); // per-scene ambient
            // Downstream systems (particles, world-UI, ...) registered for THIS scene contribute into
            // the same snapshot - render stays ignorant of their types (scene-free IRenderDataProvider).
            for (const SceneProvider& sp : m_providers)
            {
                if (sp.scene == &scene && sp.provider != nullptr)
                {
                    sp.provider->ExtractRenderData(*snapshot);
                }
            }
            if (m_probeSystem.Get() != nullptr)
            { // map probes to persistent array slots (capture in P1b)
                m_probeSystem->Assign(snapshot, snapshot->ReflectionProbes());
            }
        }

        ViewCamera camera;
        Color clearColor{0.392f, 0.584f, 0.929f, 1.0f}; // cornflower fallback (no primary camera)
        if (cameraOverride != nullptr)
        {
            camera = cameraOverride->camera;
            clearColor = cameraOverride->clearColor;
        }
        else
        {
            (void)ExtractPrimaryCamera(scene, camera, &clearColor);
        } // clear comes from the camera

        ViewSettings settings;
        settings.clear = rhi::ClearColor{clearColor.r, clearColor.g, clearColor.b, clearColor.a};
        settings.viewportX = viewport.x;
        settings.viewportY = viewport.y;
        settings.viewportWidth = viewport.width;
        settings.viewportHeight = viewport.height;
        settings.targetTexture = targetState.texture;
        settings.targetCurrentState = targetState.currentState;
        settings.targetFinalState = targetState.finalState;

        // Resolve this view's post-processing (exposure/bloom/AO). The programmatic global override
        // (samples' debug panels) wins once touched; otherwise the scene's authored PostProcessSettings
        // drive - exposure authored in EV/stops is resolved to the tonemap's linear multiplier here.
        // (AA/SSR remain frame-global for now - phase 2a.)
        if (m_globalPostActive)
        {
            settings.post.exposure = m_exposure;
            settings.post.agxTonemap = true; // the legacy global API has no operator toggle
            settings.post.bloomEnabled = m_bloomEnabled;
            settings.post.bloomThreshold = m_bloomThreshold;
            settings.post.bloomKnee = m_bloomKnee;
            settings.post.bloomIntensity = m_bloomIntensity;
            settings.post.aoMode = static_cast<u32>(m_aoMode);
            settings.post.aoStrength = m_aoStrength;
            settings.post.aoRadius = m_aoRadius;
            settings.post.aoIntensity = m_aoIntensity;
            settings.post.taaEnabled = m_taaEnabled;
            settings.post.taaBlend = m_taaBlend;
            settings.post.taaGamma = m_taaGamma;
            settings.post.fxaaEnabled = m_fxaaEnabled;
            settings.post.fxaaSubpixel = m_fxaaSubpixel;
            settings.post.ssrEnabled = m_ssrEnabled;
            settings.post.ssrIntensity = m_ssrParams.intensity;
        }
        else if (const PostProcessSystem* pp = scene.GetSystem<PostProcessSystem>())
        {
            settings.post = ResolveScenePost(pp->Post());
        }
        // Editor viewport "show flags": ephemeral per-view overrides that strip effects for editing
        // clarity, layered ON TOP of the resolved post - never written back to the scene.
        if (postOverride != nullptr)
        {
            ApplyViewPostOverride(settings.post, *postOverride);
        }
        // Finalize per-view motion-vector need AFTER any override: TAA OR an SSR temporal pass.
        // SSR's `temporal` stays frame-global, so the OR lands here, not in ResolveScenePost.
        settings.post.needsMotion =
            settings.post.taaEnabled || (settings.post.ssrEnabled && m_ssrParams.temporal);
        {
            DRACONIC_PROFILE_SCOPE("Render.AddView"); // binds the view + builds/sorts its draw list
            const void* sceneDebug =
                m_debugScenes.Find(&scene); // this scene's per-scene gizmo list (or null)
            m_frame->AddView(*snapshot, camera, settings, target, targetFormat, width, height,
                             sceneDebug,
                             /*sceneKey*/ &scene);
        }
    }

    void RenderSubsystem::EndRendering()
    {
        DRACONIC_PROFILE_SCOPE("Render.Compose");
        if (m_frame.Get() != nullptr)
        {
            m_frame->End();
        }
        // Immediate-mode: clear all debug lists AFTER rendering, so next frame's draws start empty
        // (the app accumulates during its update, before the next BeginRendering).
        m_debugGlobal.Clear();
        m_debugScreen.Clear();
        for (auto& kv : m_debugScenes)
        {
            kv.value.Clear();
        }
    }

    void RenderSubsystem::UnregisterOverlay(IScreenOverlay* overlay)
    {
        m_screenOverlays.Remove(overlay);
    }

    void RenderSubsystem::RenderOverlays(rhi::CommandEncoder& encoder, rhi::TextureView* target,
                                         rhi::TextureFormat targetFormat, u32 width, u32 height,
                                         u32 frameIndex)
    {
        if (target == nullptr || width == 0 || height == 0 || m_screenOverlays.IsEmpty())
        {
            return;
        }
        rhi::RenderPassDesc pass;
        rhi::ColorAttachment color;
        color.view = target;
        color.loadOp = rhi::LoadOp::Load;
        color.storeOp = rhi::StoreOp::Store;
        pass.colorAttachments.Add(color);
        if (rhi::RenderPassEncoder* rp = encoder.BeginRenderPass(pass))
        {
            ScreenOverlayView view;
            view.width = width;
            view.height = height;
            view.targetFormat = targetFormat;
            view.frameIndex = frameIndex;
            for (IScreenOverlay* overlay : m_screenOverlays.Items())
            {
                overlay->Render(*rp, view);
            }
            rp->End();
        }
    }

    void RenderSubsystem::OnInit()
    {
        RegisterRenderComponentReflection(); // tooling: reflected components (idempotent)

        // The ShaderSystemHost encapsulates the pack-vs-dev decision (cooked shaders.dpak beside the
        // executable => no compiler; otherwise DXC + a file provider over the engine shader root with
        // hot reload). The same host every consumer (VG/UI, ImGui) uses. Inert if neither is present.
#ifdef DRACONIC_ENGINE_SHADER_DIR
        constexpr StringView kEngineShaderRoot = u8"" DRACONIC_ENGINE_SHADER_DIR;
#else
        constexpr StringView kEngineShaderRoot = u8"Shaders";
#endif
        if (!m_shaderHost.Initialize(*m_device, kEngineShaderRoot))
        {
            return; // neither a compiler nor a pack - renderer stays inert
        }
        m_shaders = m_shaderHost.System();
        if (m_shaderHost.UsingPack())
        {
            rhi::LogInfof("RenderSubsystem: using cooked shader pack (%u variants) - no runtime "
                          "compiler",
                          static_cast<unsigned>(m_shaderHost.PackVariantCount()));
        }

        m_psoCache =
            MakeUnique<materials::PipelineStateCache>(DefaultAllocator(), *m_shaders, *m_device);
        m_materialSystem = MakeUnique<materials::MaterialSystem>(DefaultAllocator());
        if (!m_materialSystem->Initialize(*m_device).IsOk())
        {
            m_materialSystem.Reset();
            return;
        }

        m_meshRenderer = MakeUnique<MeshRenderer>(DefaultAllocator(), *m_device, *m_shaders,
                                                  *m_psoCache, *m_materialSystem, m_framesInFlight);
        if (!m_meshRenderer->Initialize().IsOk())
        {
            m_meshRenderer.Reset();
            return;
        }
        m_registry.Register(
            m_meshRenderer.Get()); // FIRST -> renderer id 0 (the RenderData default)

        // Sprites: registered after the mesh renderer (id 1); shares the blended forward pass.
        m_spriteRenderer =
            MakeUnique<SpriteRenderer>(DefaultAllocator(), *m_device, *m_shaders, m_framesInFlight);
        if (m_spriteRenderer->Initialize().IsOk())
        {
            m_registry.Register(m_spriteRenderer.Get());
        }
        else
        {
            m_spriteRenderer.Reset();
        }

        // Debug toggles: flip to false to isolate a subsystem (e.g. bisecting a rendering bug). When
        // off, the renderer falls back gracefully - clustering off => the shader's all-lights path;
        // shadows off => unshadowed. Kept as compile-time flags (zero cost when on).
        constexpr bool kEnableClusters = true;
        constexpr bool kEnableShadows = true;

        // Clustered light culling: a build compute pass per view (declared into the frame graph by
        // RenderFrame). Optional - if it fails to init, the renderer runs without clustering.
        if (kEnableClusters)
        {
            m_clusterSystem = MakeUnique<ClusterSystem>(DefaultAllocator(), *m_device, *m_shaders,
                                                        m_framesInFlight);
            if (!m_clusterSystem->Initialize().IsOk())
            {
                m_clusterSystem.Reset();
            }
        }

        // HDR resolve: forward renders linear HDR, this pass tonemaps to the LDR target. Optional -
        // if it fails to init, the renderer falls back to writing the LDR target directly.
        m_tonemapPass =
            MakeUnique<TonemapPass>(DefaultAllocator(), *m_device, *m_shaders, m_framesInFlight);
        if (!m_tonemapPass->Initialize().IsOk())
        {
            m_tonemapPass.Reset();
        }

        // Directional shadow map (phase 5). Optional - if it fails to init, the scene renders unshadowed.
        if (kEnableShadows)
        {
            m_shadowSystem =
                MakeUnique<ShadowSystem>(DefaultAllocator(), *m_device, m_framesInFlight);
            if (!m_shadowSystem->Initialize().IsOk())
            {
                m_shadowSystem.Reset();
            }
        }

        // Image-based lighting (phase 6). Optional - if it fails to init, the scene uses flat ambient.
        m_iblSystem = MakeUnique<IBLSystem>(DefaultAllocator(), *m_device, *m_shaders);
        if (!m_iblSystem->Initialize().IsOk())
        {
            m_iblSystem.Reset();
        }

        m_probeSystem =
            MakeUnique<ReflectionProbeSystem>(DefaultAllocator(), *m_device, *m_shaders);
        if (!m_probeSystem->Initialize().IsOk())
        {
            m_probeSystem.Reset();
        }

        // Visible sky (background) from the IBL environment. Optional.
        m_skyPass =
            MakeUnique<SkyPass>(DefaultAllocator(), *m_device, *m_shaders, m_framesInFlight);
        if (!m_skyPass->Initialize().IsOk())
        {
            m_skyPass.Reset();
        }

        // HDR bloom (composited at tonemap). Optional.
        m_bloomPass = MakeUnique<BloomPass>(DefaultAllocator(), *m_device, *m_shaders);
        if (!m_bloomPass->Initialize().IsOk())
        {
            m_bloomPass.Reset();
        }

        // Temporal AA resolve (per-view history). Optional.
        m_taaPass = MakeUnique<TaaPass>(DefaultAllocator(), *m_device, *m_shaders);
        if (!m_taaPass->Initialize().IsOk())
        {
            m_taaPass.Reset();
        }

        // Ambient occlusion (GTAO/SSAO from the G-buffer). Optional.
        m_aoPass = MakeUnique<AoPass>(DefaultAllocator(), *m_device, *m_shaders);
        if (!m_aoPass->Initialize().IsOk())
        {
            m_aoPass.Reset();
        }

        // Screen-space reflections (reflect the lit HDR before AO/TAA). Optional.
        m_ssrPass = MakeUnique<SsrPass>(DefaultAllocator(), *m_device, *m_shaders);
        if (!m_ssrPass->Initialize().IsOk())
        {
            m_ssrPass.Reset();
        }

        // FXAA (TAA-off fallback AA). Optional.
        m_fxaaPass =
            MakeUnique<FxaaPass>(DefaultAllocator(), *m_device, *m_shaders, m_framesInFlight);
        if (!m_fxaaPass->Initialize().IsOk())
        {
            m_fxaaPass.Reset();
        }

        // Screen-space decals (project onto depth, blend into HDR before AO/TAA). Optional.
        m_decalPass =
            MakeUnique<DecalPass>(DefaultAllocator(), *m_device, *m_shaders, m_framesInFlight);
        if (!m_decalPass->Initialize().IsOk())
        {
            m_decalPass.Reset();
        }

        // Debug draw (per-view gizmos + screen text). Optional.
        m_debugPass =
            MakeUnique<DebugDrawPass>(DefaultAllocator(), *m_device, *m_shaders, m_framesInFlight);
        if (!m_debugPass->Initialize().IsOk())
        {
            m_debugPass.Reset();
        }

        m_frame = MakeUnique<RenderFrame>(
            DefaultAllocator(), *m_device, m_registry, m_framesInFlight, m_clusterSystem.Get(),
            m_tonemapPass.Get(), m_shadowSystem.Get(), m_iblSystem.Get(), m_skyPass.Get(),
            m_bloomPass.Get(), m_taaPass.Get(), m_aoPass.Get(), m_fxaaPass.Get());
        m_frame->EnableGpuProfiling(); // per-pass GPU timestamps (cheap; read on the P-key dump)
    }

    void RenderSubsystem::OnReady()
    {
        // Register as scene-aware so we inject our managers into scenes the app creates.
        if (draconic::runtime::Context* ctx = GetContext())
        {
            if (auto* scenes = ctx->GetSubsystem<scene::SceneSubsystem>())
            {
                scenes->RegisterSceneAware(this);
            }
        }
    }

    void RenderSubsystem::OnShutdown()
    {
        if (draconic::runtime::Context* ctx = GetContext())
        {
            if (auto* scenes = ctx->GetSubsystem<scene::SceneSubsystem>())
            {
                scenes->UnregisterSceneAware(this);
            }
        }
        m_device->WaitIdle();     // GPU must finish before we free its buffers/PSOs/descriptors
        m_frame.Reset();          // releases the forward pass's per-frame GPU resources
        m_clusterSystem.Reset();  // before the ShaderSystem it borrows
        m_tonemapPass.Reset();    // before the ShaderSystem it borrows
        m_shadowSystem.Reset();   // shadow depth textures
        m_skyPass.Reset();        // before the ShaderSystem it borrows
        m_bloomPass.Reset();      // before the ShaderSystem it borrows
        m_taaPass.Reset();        // before the ShaderSystem it borrows
        m_aoPass.Reset();         // before the ShaderSystem it borrows
        m_ssrPass.Reset();        // before the ShaderSystem it borrows
        m_fxaaPass.Reset();       // before the ShaderSystem it borrows
        m_decalPass.Reset();      // before the ShaderSystem it borrows
        m_debugPass.Reset();      // before the ShaderSystem it borrows
        m_probeSystem.Reset();    // probe textures/buffers (before the ShaderSystem it borrows)
        m_iblSystem.Reset();      // IBL textures/buffers (before the ShaderSystem it borrows)
        m_spriteRenderer.Reset(); // before the ShaderSystem it borrows
        m_meshRenderer.Reset(); // before the systems it borrows (releases material instances first)
        m_materialSystem.Reset();
        m_psoCache.Reset();
        m_shaders = nullptr;      // borrowed from the host; the host owns/destroys the ShaderSystem
        m_shaderHost.Shutdown();  // after every pass that borrowed *m_shaders
    }

    ExtractedScene* RenderSubsystem::AcquireScene()
    {
        if (m_sceneCount == m_scenes.Size())
        {
            m_scenes.PushBack(MakeUnique<ExtractedScene>(DefaultAllocator()));
        }
        ExtractedScene* s = m_scenes[m_sceneCount++].Get();
        s->Reset();
        return s;
    }
}
