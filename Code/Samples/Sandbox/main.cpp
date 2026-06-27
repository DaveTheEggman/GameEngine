// Sandbox — the running dev harness. It extends DefaultApplication (which registers
// the SceneSubsystem + RenderSubsystem and renders active scenes each frame), creates a
// scene with a camera and a spinning cube, and lets the engine draw it. As the renderer
// grows, this is where we exercise it.

#include "Core/Prelude.h"
#include <cstring>

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
        bool useDistinctMaterials = true;   // set from the command line (--instanced / --distinct)

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

            // STRESS TOGGLE for parallel COMMAND recording (set from the command line, see main):
            // when true, every cube gets its OWN material, so they don't batch — ~400 distinct
            // resolved draws, which exceeds the emit threshold and fans the EMIT phase out across
            // the job system's worker threads (per-worker render bundles). When false, all cubes
            // share one material and fuse into a single instanced draw (the serial emit path).
            const bool kDistinctMaterials = useDistinctMaterials;

            // A spinning 20x20 = 400-cube grid. Either way, extraction fans out across the job
            // system (> the parallel-extraction threshold). Each cube has a distinct color.
            if (auto* meshes = m_scene->GetSystem<rd::MeshComponentManager>()) {
                rc::RefPtr<geo::StaticMesh> cube = geo::Primitives::Cube(0.42f);
                // The shared (instanced) material carries a warm BaseColor; distinct materials get
                // a per-cube BaseColor below. BaseColor is a data-driven set-2 property the
                // MaterialSystem infers + the forward shader multiplies into the shade.
                rc::RefPtr<mat::Material> shared = mat::MaterialBuilder(u8"lit").Shader(u8"forward")
                    .Color(u8"BaseColor", rc::Vec4{ 1.0f, 0.78f, 0.55f, 1.0f }).Build();

                constexpr int kGrid = 20;
                for (int y = 0; y < kGrid; ++y) {
                    for (int x = 0; x < kGrid; ++x) {
                        sc::EntityHandle e = m_scene->CreateEntity(u8"cube");
                        const rc::f32 fx = static_cast<rc::f32>(x) - (kGrid - 1) * 0.5f;
                        const rc::f32 fy = static_cast<rc::f32>(y) - (kGrid - 1) * 0.5f;
                        m_scene->SetLocalPosition(e, rc::Vec3{ fx * 1.05f, fy * 1.05f, 0.0f });
                        // Desaturated toward a light gray so the LIGHT color (not the albedo)
                        // dominates the shaded look, while keeping subtle per-cube variation.
                        const rc::Vec4 baseColor{ 0.55f + 0.25f * static_cast<rc::f32>(x) / (kGrid - 1),
                                                  0.55f + 0.25f * static_cast<rc::f32>(y) / (kGrid - 1),
                                                  0.65f, 1.0f };
                        rd::MeshComponent& mc = meshes->Add(e);
                        mc.mesh = cube;
                        // Distinct mode: the MATERIAL's BaseColor drives the per-cube color (and the
                        // per-instance tint is white). Instanced mode: one shared material; the
                        // per-instance color drives the variation.
                        mc.material = kDistinctMaterials
                            ? mat::MaterialBuilder(u8"lit").Shader(u8"forward").Color(u8"BaseColor", baseColor).Build()
                            : shared;
                        mc.color = kDistinctMaterials ? rc::Color{ 1.0f, 1.0f, 1.0f, 1.0f }
                                                      : rc::Color{ baseColor.x, baseColor.y, baseColor.z, 1.0f };
                        m_cubes.PushBack(e);
                    }
                }
            }

            // Lights: a dim directional key (down-forward) + a bright point light that orbits the
            // grid in OnUpdate, so the per-light forward shade is visible (moving highlight).
            if (auto* lights = m_scene->GetSystem<rd::LightComponentManager>()) {
                sc::EntityHandle key = m_scene->CreateEntity(u8"keyLight");
                rc::Transform kt = m_scene->GetLocalTransform(key);
                kt.rotation = rc::Quat::FromAxisAngle(rc::Vec3{ 1.0f, 0.0f, 0.0f }, -0.6f);
                m_scene->SetLocalTransform(key, kt);
                rd::LightComponent& kl = lights->Add(key);
                kl.type = rd::LightType::Directional;
                kl.color = rc::Color{ 0.5f, 0.65f, 1.0f, 1.0f };
                kl.intensity = 0.25f;                       // dim cool fill, so the point light dominates

                m_pointLight = m_scene->CreateEntity(u8"pointLight");
                rd::LightComponent& pl = lights->Add(m_pointLight);
                pl.type = rd::LightType::Point;
                pl.color = rc::Color{ 1.0f, 0.45f, 0.12f, 1.0f };  // strong orange
                pl.intensity = 12.0f;
                pl.range = 13.0f;                           // tight, so it reads as a localized pool, not a wash
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
            // Orbit the point light through the grid (close in front, z = +3.5) on its OWN slower
            // phase so its bright orange pool clearly sweeps independently of the cube spin.
            if (m_pointLight.IsAssigned()) {
                const rc::f32 a = m_angle * 0.55f;
                m_scene->SetLocalPosition(m_pointLight,
                    rc::Vec3{ 11.0f * rc::Cos(a), 11.0f * rc::Sin(a), 3.5f });
            }
        }

        void OnShutdown(rt::IApplicationHost&) override
        {
            rc::ConsoleWrite(u8"Sandbox: shutting down.\n");
        }

    private:
        sc::Scene*                  m_scene = nullptr;
        sc::EntityHandle            m_camera{};
        sc::EntityHandle            m_pointLight{};
        rc::Array<sc::EntityHandle> m_cubes;
        rc::f32                     m_angle = 0.0f;
    };
}

// Custom entry point (instead of RAPTOR_APP_MAIN) so the render mode is command-line-selectable:
//   --instanced  : all cubes share one material -> a single instanced draw (serial emit path)
//   --distinct   : every cube gets its own material -> ~400 draws -> parallel command recording
// (default: --distinct).
int main(int argc, char** argv)
{
    bool distinct = true;
    for (int i = 1; i < argc; ++i)
    {
        if (std::strcmp(argv[i], "--instanced") == 0) { distinct = false; }
        else if (std::strcmp(argv[i], "--distinct") == 0) { distinct = true; }
    }

    auto platform = rt::CreatePlatform();
    rt::GraphicsDeviceDesc gpuDesc{};
    auto gpu = rt::CreateGraphicsDevice(gpuDesc);
    rt::GraphicsDevice* device = gpu.HasValue() ? gpu.Value().Get() : nullptr;

    SandboxApp app;
    app.useDistinctMaterials = distinct;
    return rt::RunApplication(app, *platform, device);
}
