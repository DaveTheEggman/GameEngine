/// Raptor::RenderSubsystem — the `:subsystem` partition.
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

export module raptor.render.subsystem:subsystem;

import raptor.core;
import raptor.rhi;
import raptor.runtime;            // Subsystem, Context
import raptor.scene;              // Scene, ISceneAware
import raptor.scene.subsystem;    // SceneSubsystem (to register as scene-aware)
import raptor.shaders;            // Compiler
import raptor.shaders.system;     // ShaderSystem
import raptor.materials.pso;      // PipelineStateCache
import raptor.render;             // MeshRenderer, RendererRegistry, RenderFrame, ExtractedScene
import :components;
import :extract;
import :scene_renderer;

using namespace raptor::core;
namespace rhi = raptor::rhi;

export namespace raptor::render {

class RenderSubsystem final : public raptor::runtime::Subsystem,
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
    }

    [[nodiscard]] bool IsReady() const noexcept { return m_frame.Get() != nullptr; }

    // ---- ISceneRenderer ----

    void BeginRendering(rhi::CommandEncoder& encoder, u32 frameIndex) override {
        if (m_frame.Get() == nullptr) { return; }
        m_sceneCount = 0;
        // Provision per-worker extraction arenas for this frame (one per job-system slot, or a
        // single slot when the job system is absent — serial fallback).
        const u32 slotCount = HasGlobalJobSystem() ? GlobalJobs().SlotCount() : 1u;
        m_renderCtx.BeginFrame(slotCount);
        m_frame->Begin(encoder, frameIndex);
    }

    void RenderScene(scene::Scene& scene, rhi::TextureView* target, rhi::TextureFormat targetFormat,
                     u32 width, u32 height, rhi::ClearColor clear,
                     const CameraOverride* cameraOverride = nullptr) override {
        if (m_frame.Get() == nullptr || target == nullptr) { return; }

        ExtractedScene* snapshot = AcquireScene();
        ExtractSceneInto(scene, *snapshot, m_renderCtx);   // parallel when the job system is up

        ViewCamera camera;
        if (cameraOverride != nullptr) { camera = cameraOverride->camera; }
        else { (void)ExtractPrimaryCamera(scene, camera); }   // no camera -> identity (still clears)

        ViewSettings settings;
        settings.clear = clear;
        m_frame->AddView(*snapshot, camera, settings, target, targetFormat, width, height);
    }

    void EndRendering() override {
        if (m_frame.Get() != nullptr) { m_frame->End(); }
    }

protected:
    void OnInit() override {
        if (!shaders::createCompiler(shaders::CompilerDesc{}, m_compiler).IsOk() || m_compiler == nullptr) {
            return;   // no shader compiler — renderer stays inert
        }
        m_shaders  = MakeUnique<shaders::ShaderSystem>(DefaultAllocator(), *m_compiler, *m_device);
        m_psoCache = MakeUnique<materials::PipelineStateCache>(DefaultAllocator(), *m_shaders, *m_device);

        m_meshRenderer = MakeUnique<MeshRenderer>(DefaultAllocator(), *m_device, *m_shaders, *m_psoCache, m_framesInFlight);
        if (!m_meshRenderer->Initialize().IsOk()) { m_meshRenderer.Reset(); return; }
        m_registry.Register(m_meshRenderer.Get());

        m_frame = MakeUnique<RenderFrame>(DefaultAllocator(), *m_device, m_registry);
    }

    void OnReady() override {
        // Register as scene-aware so we inject our managers into scenes the app creates.
        if (raptor::runtime::Context* ctx = GetContext()) {
            if (auto* scenes = ctx->GetSubsystem<scene::SceneSubsystem>()) { scenes->RegisterSceneAware(this); }
        }
    }

    void OnShutdown() override {
        if (raptor::runtime::Context* ctx = GetContext()) {
            if (auto* scenes = ctx->GetSubsystem<scene::SceneSubsystem>()) { scenes->UnregisterSceneAware(this); }
        }
        m_device->WaitIdle();   // GPU must finish before we free its buffers/PSOs/descriptors
        m_frame.Reset();        // releases the forward pass's depth target
        m_meshRenderer.Reset(); // before the systems it borrows
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
    UniquePtr<MeshRenderer>                   m_meshRenderer;
    RendererRegistry                          m_registry;
    UniquePtr<RenderFrame>                    m_frame;

    Array<UniquePtr<ExtractedScene>>          m_scenes;       // snapshot pool
    usize                                     m_sceneCount = 0;
    RenderContext                             m_renderCtx;    // per-worker extraction arenas
};

} // namespace raptor::render
