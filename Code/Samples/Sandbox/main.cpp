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
import raptor.render;                  // ViewCamera / ViewportRect (split-screen overrides)
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

            // Per-scene environment ambient (a dim cool indirect term; IBL replaces it later).
            if (auto* env = m_scene->GetSystem<rd::EnvironmentSystem>()) {
                env->Environment().ambientColor     = rc::Color{ 0.12f, 0.16f, 0.28f, 1.0f };
                env->Environment().ambientIntensity = 0.35f;
            }

            // camera, pulled back along +Z looking at the origin (down -Z by default)
            // Raised + pitched down so the horizontal floor (lights above it) is clearly in view,
            // with the cube grids standing on it. Pitch ~28 deg below horizontal (looks toward the
            // scene center). Default camera looks down -Z; rotating about +X by -pitch tilts it down.
            m_camera = m_scene->CreateEntity(u8"camera");
            m_scene->SetLocalPosition(m_camera, rc::Vec3{ 0.0f, 14.0f, 30.0f });
            rc::Transform camT = m_scene->GetLocalTransform(m_camera);
            camT.rotation = rc::Quat::FromAxisAngle(rc::Vec3{ 1.0f, 0.0f, 0.0f }, -0.48f);
            m_scene->SetLocalTransform(m_camera, camT);
            if (auto* cameras = m_scene->GetSystem<rd::CameraComponentManager>()) {
                rd::CameraComponent& cam = cameras->Add(m_camera);   // default 60deg perspective
                cam.clearColor = rc::Color{ 0.02f, 0.02f, 0.03f, 1.0f };   // dark backdrop so the lit scene reads
            }

            // A large horizontal floor (Plane normal = +Y) under the scene — the point lights hover
            // above it and cast visible pools on it. The two cube grids stand on the floor.
            if (auto* meshes = m_scene->GetSystem<rd::MeshComponentManager>()) {
                sc::EntityHandle floor = m_scene->CreateEntity(u8"floor");
                m_scene->SetLocalPosition(floor, rc::Vec3{ 0.0f, -7.0f, 0.0f });
                rd::MeshComponent& fmc = meshes->Add(floor);
                fmc.mesh = geo::Primitives::Plane(120.0f, 120.0f);
                fmc.material = mat::MaterialBuilder(u8"lit").Shader(u8"forward")
                    .Color(u8"BaseColor", rc::Vec4{ 0.5f, 0.5f, 0.53f, 1.0f })
                    .Float(u8"Metallic", 0.0f).Float(u8"Roughness", 0.65f).Build();

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
                kl.color = rc::Color{ 0.4f, 0.5f, 0.7f, 1.0f };
                kl.intensity = 0.12f;                       // very dim fill; the point lights dominate

                // A field of point lights hovering above the floor (X-Z grid) — the clustered
                // light-culling demo. Each fragment only evaluates the lights in its froxel, so this
                // scales far better than an all-lights loop. Each casts a colored pool on the floor.
                constexpr int kCols = 6, kRows = 3;         // 18 point lights over the floor
                for (int j = 0; j < kRows; ++j) {
                    for (int i = 0; i < kCols; ++i) {
                        sc::EntityHandle e = m_scene->CreateEntity(u8"pointLight");
                        const rc::f32 fi = static_cast<rc::f32>(i) / (kCols - 1);
                        const rc::f32 fj = static_cast<rc::f32>(j) / (kRows - 1);
                        const rc::Vec3 base{ -15.0f + 30.0f * fi, -1.5f, -2.0f + 16.0f * fj };  // hover above floor
                        m_scene->SetLocalPosition(e, base);
                        rd::LightComponent& pl = lights->Add(e);
                        pl.type = rd::LightType::Point;
                        pl.color = rc::Color{ 0.4f + 0.6f * fi, 0.4f + 0.6f * fj, 1.0f - 0.6f * fi, 1.0f };
                        pl.intensity = 22.0f;
                        pl.range = 8.0f;                    // floor pool radius ~= sqrt(range^2 - dist^2)
                        m_pointLights.PushBack(e);
                        m_lightBases.PushBack(base);
                    }
                }
            }

            rc::ConsoleWrite(u8"Sandbox: split-screen — same scene from two cameras, 18 clustered "
                             u8"point lights. Close to exit.\n");
        }

        // Split-screen: render the one scene TWICE — left half from one camera, right half from
        // another — into the same backbuffer. Exercises the full multi-view path (two RenderViews,
        // two cluster builds, two viewports, first-clears-rest-loads) every frame.
        void OnRenderWindow(rt::IApplicationHost& host, rt::FrameContext& frame) override
        {
            auto* render = host.Ctx().GetSubsystem<rd::RenderSubsystem>();
            if (m_scene == nullptr || render == nullptr || !render->IsReady() ||
                frame.encoder == nullptr || frame.backbufferView == nullptr || frame.window == nullptr) {
                return;
            }
            auto fmt = frame.window->Swap()->Format();
            const rc::u32 halfW  = frame.width / 2;
            const rc::f32 aspect = static_cast<rc::f32>(halfW) / static_cast<rc::f32>(frame.height);

            auto makeCam = [&](rc::Vec3 eye) {
                rd::ViewCamera vc;
                vc.view       = rc::Mat4::LookAtRH(eye, rc::Vec3{ 0.0f, -2.0f, 0.0f }, rc::Vec3{ 0.0f, 1.0f, 0.0f });
                vc.projection = rc::Mat4::PerspectiveFovRH(1.0472f, aspect, 0.1f, 1000.0f);
                vc.position   = eye;
                vc.farZ       = 1000.0f;
                return vc;
            };

            rd::CameraOverride camL; camL.camera = makeCam(rc::Vec3{ -6.0f, 14.0f, 30.0f });
            camL.clearColor = rc::Color{ 0.02f, 0.02f, 0.03f, 1.0f };
            rd::CameraOverride camR; camR.camera = makeCam(rc::Vec3{  6.0f, 14.0f, 30.0f });
            camR.clearColor = rc::Color{ 0.02f, 0.02f, 0.03f, 1.0f };

            render->BeginRendering(*frame.encoder, frame.frameIndex);
            render->RenderScene(*m_scene, frame.backbufferView, fmt, frame.width, frame.height,
                                rd::ViewportRect{ 0, 0, halfW, frame.height }, &camL);
            render->RenderScene(*m_scene, frame.backbufferView, fmt, frame.width, frame.height,
                                rd::ViewportRect{ static_cast<rc::i32>(halfW), 0, frame.width - halfW, frame.height }, &camR);
            render->EndRendering();
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
            // Bob each point light in Z (depth) on its own phase, so the lights cross froxel depth
            // slices every frame — exercising the per-frame cluster rebuild, not a static binning.
            // Sweep the whole light field left/right (so the colored pools clearly slide as a
            // group — easy confirmation that pools exist and track the lights) + a gentle Z-bob.
            const rc::f32 sweep = 5.0f * rc::Sin(m_angle * 0.6f);
            for (rc::usize k = 0; k < m_pointLights.Size(); ++k) {
                rc::Vec3 p = m_lightBases[k];
                p.x += sweep;
                p.z += 0.8f * rc::Sin(m_angle * 1.3f + static_cast<rc::f32>(k) * 0.5f);
                m_scene->SetLocalPosition(m_pointLights[k], p);
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
        rc::Array<sc::EntityHandle> m_pointLights;
        rc::Array<rc::Vec3>         m_lightBases;
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
