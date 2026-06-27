// Raptor::RuntimeDefaultApp — the `raptor.runtime.defaultapp` module.
//
// DefaultApplication: an opinionated IApplication base that registers the standard
// engine subsystems. A game that wants the batteries-included engine writes
// `class MyGame : DefaultApplication` and adds its own subsystems in Configure
// (calling the base first); a game that wants only its own subsystems implements
// IApplication directly and links none of this.
//
// This lives in its OWN library — separate from raptor.runtime.client — precisely
// so the base client never pulls in the engine subsystem libraries. As the
// standard subsystems (input/scene/render/...) land, this library gains the
// dependencies; the base client stays lean. Stub for now (no subsystems exist).

module;
#include "Core/Prelude.h"

export module raptor.runtime.defaultapp;

import raptor.core;
import raptor.rhi;
import raptor.runtime.client;     // IApplication, IApplicationHost
import raptor.runtime.graphics;   // GraphicsDevice, FrameContext
import raptor.scene;              // Scene
import raptor.scene.subsystem;    // SceneSubsystem (the standard scene driver)
import raptor.render.subsystem;   // RenderSubsystem (the standard renderer)

namespace rhi = raptor::rhi;

export namespace raptor::runtime
{
    class DefaultApplication : public IApplication
    {
    public:
        // Registers the standard engine subsystems. A game subclass overrides this,
        // calls DefaultApplication::Configure(host) first, then adds its own.
        void Configure(IApplicationHost& host) override
        {
            host.Ctx().AddSubsystem<raptor::scene::SceneSubsystem>();
            if (GraphicsDevice* gfx = host.Graphics(); gfx != nullptr && gfx->Raw() != nullptr)
            {
                host.Ctx().AddSubsystem<raptor::render::RenderSubsystem>(*gfx->Raw());
            }
        }

        // Default render: draw every active scene into the window via the RenderSubsystem.
        // A game overrides this for custom rendering. (Single-scene for now — multiple
        // active scenes would each clear; compositing is a later concern.)
        void OnRenderWindow(IApplicationHost& host, FrameContext& frame) override
        {
            auto* render = host.Ctx().GetSubsystem<raptor::render::RenderSubsystem>();
            auto* scenes = host.Ctx().GetSubsystem<raptor::scene::SceneSubsystem>();
            if (render == nullptr || !render->IsReady() || scenes == nullptr ||
                frame.encoder == nullptr || frame.backbufferView == nullptr || frame.window == nullptr)
            {
                frame.Clear(0.08f, 0.09f, 0.12f, 1.0f);   // no renderer — present a clear
                return;
            }

            const rhi::TextureFormat colorFormat = frame.window->Swap()->Format();
            render->BeginRendering(*frame.encoder, frame.frameIndex);
            for (raptor::scene::Scene* scene : scenes->ActiveScenes())
            {
                render->RenderScene(*scene, frame.backbufferView, colorFormat,
                                    frame.width, frame.height, rhi::ClearColor::CornflowerBlue());
            }
            render->EndRendering();
        }
    };
}
