// Sandbox — the running dev harness. It extends DefaultApplication (which registers
// the SceneSubsystem + RenderSubsystem and renders active scenes each frame), creates a
// scene with a camera and a spinning cube, and lets the engine draw it. As the renderer
// grows, this is where we exercise it.

#include "Core/Prelude.h"
#include "Runtime/Client/AppMain.h"

import raptor.core;
import raptor.runtime;
import raptor.runtime.client;
import raptor.runtime.platform;
import raptor.runtime.platform.desktop;
import raptor.runtime.graphics;
import raptor.runtime.graphics.gpu;
import raptor.runtime.defaultapp;     // DefaultApplication (scene + render subsystems)
import raptor.scene;
import raptor.scene.subsystem;
import raptor.render.subsystem;       // MeshComponent / CameraComponent + their managers
import raptor.geometry;
import raptor.materials;

namespace rc = raptor::core;
namespace rt = raptor::runtime;
namespace sc = raptor::scene;
namespace rd = raptor::render;
namespace geo = raptor::geometry;
namespace mat = raptor::materials;

namespace
{
    class SandboxApp final : public rt::DefaultApplication
    {
    public:
        void OnStartup(rt::IApplicationHost& host) override
        {
            auto* scenes = host.Ctx().GetSubsystem<sc::SceneSubsystem>();
            if (scenes == nullptr) { return; }

            // CreateScene triggers the RenderSubsystem to inject the render managers.
            m_scene = scenes->CreateScene(u8"sandbox");

            // camera, pulled back along +Z looking at the origin (down -Z by default)
            m_camera = m_scene->CreateEntity(u8"camera");
            m_scene->SetLocalPosition(m_camera, rc::Vec3{ 0.0f, 0.0f, 4.0f });
            if (auto* cameras = m_scene->GetSystem<rd::CameraComponentManager>()) {
                cameras->Add(m_camera);   // default 60deg perspective
            }

            // a spinning cube at the origin, drawn with the built-in forward shader
            m_cube = m_scene->CreateEntity(u8"cube");
            if (auto* meshes = m_scene->GetSystem<rd::MeshComponentManager>()) {
                rd::MeshComponent& mc = meshes->Add(m_cube);
                mc.mesh     = geo::Primitives::Cube(1.0f);
                mc.material = mat::MaterialBuilder(u8"lit").Shader(u8"forward").Build();
            }

            rc::ConsoleWrite(u8"Sandbox: spinning cube. Close the window to exit.\n");
        }

        void OnUpdate(rt::IApplicationHost&, rc::f32 deltaTime) override
        {
            if (m_scene == nullptr) { return; }
            m_angle += deltaTime;
            rc::Transform t;
            t.rotation = rc::Quat::FromAxisAngle(rc::Vec3{ 0.3f, 1.0f, 0.0f }, m_angle);
            m_scene->SetLocalTransform(m_cube, t);
        }

        void OnShutdown(rt::IApplicationHost&) override
        {
            rc::ConsoleWrite(u8"Sandbox: shutting down.\n");
        }

    private:
        sc::Scene*       m_scene = nullptr;
        sc::EntityHandle m_camera{};
        sc::EntityHandle m_cube{};
        rc::f32          m_angle = 0.0f;
    };
}

RAPTOR_APP_MAIN(SandboxApp)
