/// Raptor::RenderSubsystem — the `:subsystem` partition.
///
/// RenderSubsystem: the Context-level driver that connects scenes to the (scene-agnostic)
/// renderer. It owns the GPU systems — the DXC compiler, ShaderSystem, PipelineStateCache,
/// and the ForwardRenderer — and, as an ISceneAware, injects the mesh/camera component
/// managers into each scene on creation. The app calls RenderScene() in its render
/// callback with the frame's target; the subsystem extracts the scene to a render::
/// ExtractedView and pushes it to the ForwardRenderer. (No MaterialSystem yet — the
/// built-in forward shader binds no material set; that lands with material binding.)

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
import raptor.render;             // ForwardRenderer, ExtractedView
import :components;
import :extract;

using namespace raptor::core;
namespace rhi = raptor::rhi;

export namespace raptor::render {

class RenderSubsystem final : public raptor::runtime::Subsystem, public scene::ISceneAware {
public:
    explicit RenderSubsystem(rhi::Device& device) noexcept : m_device(&device) {}

    [[nodiscard]] i32 UpdateOrder() const noexcept override { return 1000; }   // late (renders, doesn't tick)

    // Injects the render component managers into each new scene.
    void OnSceneCreated(scene::Scene& scene) override {
        scene.AddSystem<MeshComponentManager>();
        scene.AddSystem<CameraComponentManager>();
    }

    // Renders `scene` into a color target (called by the app in its render callback,
    // after the scene has ticked so transforms are current).
    void RenderScene(scene::Scene& scene, rhi::CommandEncoder& encoder, rhi::TextureView* colorTarget,
                     rhi::TextureFormat colorFormat, u32 width, u32 height, rhi::ClearColor bg) {
        if (m_forward.Get() == nullptr) { return; }
        ExtractedView view = ExtractScene(scene);
        m_forward->Render(view, encoder, colorTarget, colorFormat, width, height, bg);
    }

    [[nodiscard]] bool IsReady() const noexcept { return m_forward.Get() != nullptr; }

protected:
    void OnInit() override {
        if (!shaders::createCompiler(shaders::CompilerDesc{}, m_compiler).IsOk() || m_compiler == nullptr) {
            return;   // no shader compiler — renderer stays inert
        }
        m_shaders  = MakeUnique<shaders::ShaderSystem>(DefaultAllocator(), *m_compiler, *m_device);
        m_psoCache = MakeUnique<materials::PipelineStateCache>(DefaultAllocator(), *m_shaders, *m_device);
        m_forward  = MakeUnique<ForwardRenderer>(DefaultAllocator(), *m_device, *m_shaders, *m_psoCache);
        if (!m_forward->Initialize().IsOk()) { m_forward.Reset(); return; }
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
        m_forward.Reset();      // before the systems it borrows
        m_psoCache.Reset();
        m_shaders.Reset();
        if (m_compiler != nullptr) { m_compiler->Destroy(); m_compiler = nullptr; }
    }

private:
    rhi::Device*       m_device;
    shaders::Compiler* m_compiler = nullptr;
    UniquePtr<shaders::ShaderSystem>          m_shaders;
    UniquePtr<materials::PipelineStateCache>  m_psoCache;
    UniquePtr<ForwardRenderer>                m_forward;
};

} // namespace raptor::render
