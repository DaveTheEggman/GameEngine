// Draconic::RuntimeDefaultApp - the `draconic.runtime.defaultapp` module.
//
// DefaultApplication: an opinionated IApplication base that registers the standard
// engine subsystems. A game that wants the batteries-included engine writes
// `class MyGame : DefaultApplication` and adds its own subsystems in Configure
// (calling the base first); a game that wants only its own subsystems implements
// IApplication directly and links none of this.
//
// This lives in its OWN library - separate from draconic.runtime.client - precisely
// so the base client never pulls in the engine subsystem libraries. As the
// standard subsystems (input/scene/render/...) land, this library gains the
// dependencies; the base client stays lean. Stub for now (no subsystems exist).

module;
#include "Core/Prelude.h"

export module draconic.runtime.defaultapp;

import draconic.core;
import draconic.rhi;
import draconic.runtime.client;     // IApplication, IApplicationHost
import draconic.shell;   // IShell, IKeyboard, KeyCode (the profile-dump hotkey)
import draconic.graphics;   // GraphicsDevice, FrameContext
import draconic.scene;              // Scene
import draconic.scene.subsystem;    // SceneSubsystem (the standard scene driver)
import draconic.render.subsystem;   // RenderSubsystem (the standard renderer)
import draconic.animation.subsystem; // AnimationSubsystem (drives skeletal animation from the scene)
import draconic.profiler;           // the CPU scope profiler (P-key dump)

namespace rhi = draconic::rhi;
namespace core  = draconic::core;
using namespace draconic::shell;   // IShell + input/window types (moved from draconic::runtime)
using namespace draconic::graphics;   // GraphicsDevice/RenderWindow/FrameContext (moved from draconic::runtime)

export namespace draconic::runtime
{
    class DefaultApplication : public IApplication
    {
    public:
        // Press P to print the previous frame's CPU scope tree + per-pass GPU timing. A game
        // subclass that overrides OnUpdate should call DefaultApplication::OnUpdate(host, dt) to
        // keep the hotkey. (Reads the GPU timestamps after a device stall - fine for an on-demand dump.)
        void OnUpdate(IApplicationHost& host, core::f32 /*deltaTime*/) override
        {
            IShell* plat = host.Shell();
            IInputManager* input = (plat != nullptr) ? plat->Input() : nullptr;
            IKeyboard* kb = (input != nullptr) ? input->Keyboard() : nullptr;
            if (kb == nullptr || !kb->IsKeyPressed(KeyCode::P)) { return; }

            core::ConsoleWrite(draconic::profiler::Profiler::Get().BuildReport().AsView());
            if (auto* renderer = host.Ctx().GetSubsystem<draconic::render::RenderSubsystem>())
            {
                core::String gpu;
                renderer->BuildGpuProfileReport(gpu);
                core::ConsoleWrite(gpu.AsView());
            }
        }

        // Registers the standard engine subsystems. A game subclass overrides this,
        // calls DefaultApplication::Configure(host) first, then adds its own.
        void Configure(IApplicationHost& host) override
        {
            host.Ctx().AddSubsystem<draconic::scene::SceneSubsystem>();
            if (GraphicsDevice* gfx = host.Graphics(); gfx != nullptr && gfx->Raw() != nullptr)
            {
                host.Ctx().AddSubsystem<draconic::render::RenderSubsystem>(*gfx->Raw(), gfx->FramesInFlight());
                // Drives skeletal animation from the scene tick (injects the SkeletalAnimation manager,
                // ticks players, feeds bone matrices to mesh components). Needs the render managers.
                host.Ctx().AddSubsystem<draconic::animation::AnimationSubsystem>();
            }
        }

        // Default render: draw every active scene into the window via the RenderSubsystem.
        // A game overrides this for custom rendering. (Single-scene for now - multiple
        // active scenes would each clear; compositing is a later concern.)
        void OnRenderWindow(IApplicationHost& host, FrameContext& frame) override
        {
            auto* render = host.Ctx().GetSubsystem<draconic::render::RenderSubsystem>();
            auto* scenes = host.Ctx().GetSubsystem<draconic::scene::SceneSubsystem>();
            if (render == nullptr || !render->IsReady() || scenes == nullptr ||
                frame.encoder == nullptr || frame.backbufferView == nullptr || frame.window == nullptr)
            {
                frame.Clear(0.08f, 0.09f, 0.12f, 1.0f);   // no renderer - present a clear
                return;
            }

            const rhi::TextureFormat colorFormat = frame.window->Swap()->Format();
            render->BeginRendering(*frame.encoder, frame.frameIndex);
            for (draconic::scene::Scene* scene : scenes->ActiveScenes())
            {
                render->RenderScene(*scene, frame.backbufferView, colorFormat,
                                    frame.width, frame.height);   // clear comes from the scene's camera
            }
            render->EndRendering();
        }
    };
}
