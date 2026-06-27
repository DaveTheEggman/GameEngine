// Sandbox — the running dev harness we grow the engine in. It extends
// DefaultApplication (which registers the standard engine subsystems — currently the
// SceneSubsystem), creates a scene to develop against, and clears the window each
// frame. As the renderer lands, this app attaches mesh/camera components and the
// RenderSubsystem draws the scene; for now it stands up the world and presents.

#include "Core/Prelude.h"
#include "Runtime/Client/AppMain.h"

import raptor.core;
import raptor.runtime;
import raptor.runtime.client;
import raptor.runtime.platform;
import raptor.runtime.platform.desktop;
import raptor.runtime.graphics;       // GraphicsDevice + FrameContext
import raptor.runtime.graphics.gpu;   // CreateGraphicsDevice
import raptor.runtime.defaultapp;     // DefaultApplication (registers SceneSubsystem)
import raptor.scene;
import raptor.scene.subsystem;

namespace rc = raptor::core;
namespace rt = raptor::runtime;
namespace sc = raptor::scene;

namespace
{
    class SandboxApp final : public rt::DefaultApplication
    {
    public:
        // DefaultApplication::Configure registers the SceneSubsystem; a game would add
        // its own subsystems after calling the base. (We rely on the base for now.)
        void Configure(rt::IApplicationHost& host) override
        {
            rt::DefaultApplication::Configure(host);
            // TODO: host.Ctx().AddSubsystem<RenderSubsystem>(...) once the renderer lands.
        }

        void OnStartup(rt::IApplicationHost& host) override
        {
            auto* scenes = host.Ctx().GetSubsystem<sc::SceneSubsystem>();
            if (scenes != nullptr)
            {
                m_scene = scenes->CreateScene(u8"sandbox");
                m_root = m_scene->CreateEntity(u8"root");
                // TODO: m_scene->AddSystem<MeshComponentManager>()... attach Primitives::Cube,
                // add a camera entity, once the renderer's components exist.
                rc::ConsoleWrite(u8"Sandbox: scene 'sandbox' created. Close the window to exit.\n");
            }
        }

        void OnUpdate(rt::IApplicationHost&, rc::f32 deltaTime) override
        {
            // The SceneSubsystem ticks the scene for us (UpdateOrder -500). Drive
            // anything app-specific here as systems land.
            m_elapsed += deltaTime;
        }

        void OnRenderWindow(rt::IApplicationHost&, rt::FrameContext& frame) override
        {
            // No renderer yet — present a calm clear. The RenderSubsystem will draw
            // the scene into `frame` once it exists.
            frame.Clear(0.08f, 0.09f, 0.12f, 1.0f);
        }

        void OnShutdown(rt::IApplicationHost&) override
        {
            rc::ConsoleWrite(u8"Sandbox: shutting down.\n");
        }

    private:
        sc::Scene*        m_scene = nullptr;
        sc::EntityHandle  m_root{};
        rc::f32           m_elapsed = 0.0f;
    };
}

RAPTOR_APP_MAIN(SandboxApp)
