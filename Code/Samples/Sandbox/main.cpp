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
            m_scene->SetLocalPosition(m_camera, rc::Vec3{ 0.0f, 0.0f, 22.0f });
            if (auto* cameras = m_scene->GetSystem<rd::CameraComponentManager>()) {
                cameras->Add(m_camera);   // default 60deg perspective
            }

            // STRESS TOGGLE for parallel COMMAND recording: when true, every cube gets its OWN
            // material, so they don't batch — ~400 distinct resolved draws, which exceeds the
            // emit threshold and fans the EMIT phase out across the job system's worker threads
            // (per-worker render bundles). When false, all cubes share one material and fuse into
            // a single instanced draw (the serial emit path).
            constexpr bool kDistinctMaterials = true;

            // A spinning 20x20 = 400-cube grid. Either way, extraction fans out across the job
            // system (> the parallel-extraction threshold). Each cube has a distinct color.
            if (auto* meshes = m_scene->GetSystem<rd::MeshComponentManager>()) {
                rc::RefPtr<geo::StaticMesh> cube = geo::Primitives::Cube(0.42f);
                rc::RefPtr<mat::Material>   shared = mat::MaterialBuilder(u8"lit").Shader(u8"forward").Build();

                constexpr int kGrid = 20;
                for (int y = 0; y < kGrid; ++y) {
                    for (int x = 0; x < kGrid; ++x) {
                        sc::EntityHandle e = m_scene->CreateEntity(u8"cube");
                        const rc::f32 fx = static_cast<rc::f32>(x) - (kGrid - 1) * 0.5f;
                        const rc::f32 fy = static_cast<rc::f32>(y) - (kGrid - 1) * 0.5f;
                        m_scene->SetLocalPosition(e, rc::Vec3{ fx * 1.05f, fy * 1.05f, 0.0f });
                        rd::MeshComponent& mc = meshes->Add(e);
                        mc.mesh     = cube;
                        mc.material = kDistinctMaterials ? mat::MaterialBuilder(u8"lit").Shader(u8"forward").Build()
                                                         : shared;
                        mc.color    = rc::Color{ static_cast<rc::f32>(x) / (kGrid - 1),
                                                 static_cast<rc::f32>(y) / (kGrid - 1), 0.6f, 1.0f };
                        m_cubes.PushBack(e);
                    }
                }
            }

            rc::ConsoleWrite(kDistinctMaterials
                ? u8"Sandbox: 400 cubes, distinct materials -> parallel command recording. Close to exit.\n"
                : u8"Sandbox: 400 instanced cubes (shared material). Close to exit.\n");
        }

        void OnUpdate(rt::IApplicationHost&, rc::f32 deltaTime) override
        {
            if (m_scene == nullptr) { return; }
            m_angle += deltaTime;
            const rc::Quat spin = rc::Quat::FromAxisAngle(rc::Vec3{ 0.3f, 1.0f, 0.0f }, m_angle);
            for (sc::EntityHandle cube : m_cubes) {
                rc::Transform t = m_scene->GetLocalTransform(cube);
                t.rotation = spin;
                m_scene->SetLocalTransform(cube, t);
            }
        }

        void OnShutdown(rt::IApplicationHost&) override
        {
            rc::ConsoleWrite(u8"Sandbox: shutting down.\n");
        }

    private:
        sc::Scene*                  m_scene = nullptr;
        sc::EntityHandle            m_camera{};
        rc::Array<sc::EntityHandle> m_cubes;
        rc::f32                     m_angle = 0.0f;
    };
}

RAPTOR_APP_MAIN(SandboxApp)
