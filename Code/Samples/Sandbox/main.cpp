// Sandbox — the running dev harness. It extends DefaultApplication (which registers
// the SceneSubsystem + RenderSubsystem and renders active scenes each frame), creates a
// scene with two spinning cube grids (instanced + distinct), and lets the engine draw it.
// As the renderer grows, this is where we exercise it.

#include "Core/Prelude.h"

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
                rd::CameraComponent& cam = cameras->Add(m_camera);   // default 60deg perspective
                cam.clearColor = rc::Color{ 0.02f, 0.02f, 0.03f, 1.0f };   // dark backdrop so the lit cubes read
            }

            // Two side-by-side spinning grids, exercising BOTH draw paths every frame: the LEFT
            // grid is instanced (one shared material -> a single instanced draw; the per-instance
            // color supplies each cube's hue), the RIGHT grid is distinct (each cube its own
            // material with a per-cube PBR matrix -> ~256 draws -> parallel command recording).
            if (auto* meshes = m_scene->GetSystem<rd::MeshComponentManager>()) {
                rc::RefPtr<geo::StaticMesh> cube = geo::Primitives::Cube(0.35f);
                BuildGrid(*meshes, cube, /*originX*/ -8.0f, /*instanced*/ true);
                BuildGrid(*meshes, cube, /*originX*/  8.0f, /*instanced*/ false);
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
                kl.intensity = 0.5f;                        // cool fill so the vivid albedo reads

                m_pointLight = m_scene->CreateEntity(u8"pointLight");
                rd::LightComponent& pl = lights->Add(m_pointLight);
                pl.type = rd::LightType::Point;
                pl.color = rc::Color{ 1.0f, 0.45f, 0.12f, 1.0f };  // strong orange
                pl.intensity = 30.0f;                       // bright (PBR diffuse carries 1/pi)
                pl.range = 13.0f;                           // tight, so it reads as a localized pool, not a wash
            }

            rc::ConsoleWrite(u8"Sandbox: two grids — left instanced (1 draw), right distinct "
                             u8"(per-cube PBR, parallel recording). Close to exit.\n");
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

        // Builds one kGrid×kGrid spinning cube grid centered at (originX, 0). When `instanced`,
        // every cube shares one material (-> a single instanced draw; the per-instance color
        // supplies each cube's hue). Otherwise each cube gets its own material with a per-cube PBR
        // matrix (roughness sweeps smooth→rough across X, top half metallic) -> many distinct
        // draws -> parallel command recording. Both grids run every frame.
        void BuildGrid(rd::MeshComponentManager& meshes, const rc::RefPtr<geo::StaticMesh>& cube,
                       rc::f32 originX, bool instanced)
        {
            constexpr int    kGrid    = 16;     // 256 cubes -> the distinct grid trips the parallel-emit threshold
            constexpr rc::f32 kSpacing = 0.8f;

            rc::RefPtr<mat::Material> shared;
            if (instanced) {
                // White base + middling PBR; the per-instance color supplies each cube's hue.
                shared = mat::MaterialBuilder(u8"lit").Shader(u8"forward")
                    .Color(u8"BaseColor", rc::Vec4{ 1.0f, 1.0f, 1.0f, 1.0f })
                    .Float(u8"Metallic", 0.0f)
                    .Float(u8"Roughness", 0.4f).Build();
            }

            for (int y = 0; y < kGrid; ++y) {
                for (int x = 0; x < kGrid; ++x) {
                    sc::EntityHandle e = m_scene->CreateEntity(u8"cube");
                    const rc::f32 fx = static_cast<rc::f32>(x) - (kGrid - 1) * 0.5f;
                    const rc::f32 fy = static_cast<rc::f32>(y) - (kGrid - 1) * 0.5f;
                    m_scene->SetLocalPosition(e, rc::Vec3{ originX + fx * kSpacing, fy * kSpacing, 0.0f });

                    const rc::Vec4 baseColor{ static_cast<rc::f32>(x) / (kGrid - 1),
                                              static_cast<rc::f32>(y) / (kGrid - 1), 0.6f, 1.0f };
                    rd::MeshComponent& mc = meshes.Add(e);
                    mc.mesh = cube;
                    if (instanced) {
                        mc.material = shared;
                        mc.color = rc::Color{ baseColor.x, baseColor.y, baseColor.z, 1.0f };   // per-instance hue
                    } else {
                        const rc::f32 rough = 0.05f + 0.95f * static_cast<rc::f32>(x) / (kGrid - 1);
                        const rc::f32 metal = (y >= kGrid / 2) ? 1.0f : 0.0f;
                        mc.material = mat::MaterialBuilder(u8"lit").Shader(u8"forward")
                            .Color(u8"BaseColor", baseColor)
                            .Float(u8"Metallic", metal)
                            .Float(u8"Roughness", rough).Build();
                        mc.color = rc::Color{ 1.0f, 1.0f, 1.0f, 1.0f };
                    }
                    m_cubes.PushBack(e);
                }
            }
        }

    private:
        sc::Scene*                  m_scene = nullptr;
        sc::EntityHandle            m_camera{};
        sc::EntityHandle            m_pointLight{};
        rc::Array<sc::EntityHandle> m_cubes;
        rc::f32                     m_angle = 0.0f;
    };
}

int main(int, char**)
{
    auto platform = rt::CreatePlatform();
    rt::GraphicsDeviceDesc gpuDesc{};
    auto gpu = rt::CreateGraphicsDevice(gpuDesc);
    rt::GraphicsDevice* device = gpu.HasValue() ? gpu.Value().Get() : nullptr;

    SandboxApp app;
    return rt::RunApplication(app, *platform, device);
}
