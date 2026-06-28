// RenderStressTest — a deliberate worst-case renderer benchmark, ported from Sedulous's
// EngineRenderStressTest. It keeps us honest as rendering features land: a flat grid of
// spheres positioned so the camera sees ALL of them at once (frustum culling can't help),
// growable 8000 at a time. Two axes of stress:
//
//   * Batching   — by default every sphere shares ONE material + mesh, so the renderer
//                  should collapse them into a single instanced draw. Press U to give each
//                  sphere its OWN material (unique hue) → defeats batching → a draw per sphere.
//   * Static opt — press B for a sin-wave bob that rewrites EVERY sphere's transform each
//                  frame, so nothing can be cached as static (full extraction every frame).
//
// No HUD yet (UI/VG deferred): stats print to the console once per second (toggle H), and the
// inherited P key dumps the CPU scope tree + per-pass GPU timings. Fly camera: WASD/QE move,
// hold RMB (or Tab to capture) to look, Shift to move fast, Esc to exit.

#include "Core/Prelude.h"

import raptor.core;
import raptor.runtime;
import raptor.runtime.client;
import raptor.runtime.platform;
import raptor.runtime.platform.desktop;
import raptor.runtime.graphics;
import raptor.runtime.graphics.gpu;
import raptor.runtime.defaultapp;
import raptor.scene;
import raptor.scene.subsystem;
import raptor.render.subsystem;
import raptor.geometry;
import raptor.materials;

namespace rc  = raptor::core;
namespace rt  = raptor::runtime;
namespace sc  = raptor::scene;
namespace rd  = raptor::render;
namespace geo = raptor::geometry;
namespace mat = raptor::materials;

namespace
{
    class StressTestApp final : public rt::DefaultApplication
    {
        static constexpr rc::i32 kSpheresPerBatch = 8000;
        static constexpr rc::f32 kSphereSpacing   = 1.5f;
        static constexpr rc::f32 kLookSens        = 0.003f;

    public:
        void OnStartup(rt::IApplicationHost& host) override
        {
            auto* scenes = host.Ctx().GetSubsystem<sc::SceneSubsystem>();
            if (scenes == nullptr) { return; }
            m_scene = scenes->CreateScene(u8"stress");

            // A modest ambient so unlit-facing hemispheres aren't pure black.
            if (auto* env = m_scene->GetSystem<rd::EnvironmentSystem>()) {
                env->Environment().ambientColor     = rc::Color{ 0.10f, 0.12f, 0.16f, 1.0f };
                env->Environment().ambientIntensity = 0.30f;
            }

            // Shared sphere material (gray PBR). Every sphere points at THIS one by default, so the
            // renderer can batch them. Unique mode (U) builds a per-sphere material instead.
            m_sharedMat = mat::MaterialBuilder(u8"stress.shared").Shader(u8"forward")
                .Color(u8"BaseColor", rc::Vec4{ 0.7f, 0.7f, 0.7f, 1.0f })
                .Float(u8"Metallic", 0.1f).Float(u8"Roughness", 0.4f).Build();

            // One sphere mesh, shared by all instances (matches Sedulous: radius 0.5, 16x8).
            m_sphere = geo::Primitives::Sphere(0.5f, 16, 8);

            // Large ground plane so the bobbing spheres read against a surface.
            if (auto* meshes = m_scene->GetSystem<rd::MeshComponentManager>()) {
                sc::EntityHandle ground = m_scene->CreateEntity(u8"ground");
                m_scene->SetLocalPosition(ground, rc::Vec3{ 0.0f, 0.0f, 0.0f });
                rd::MeshComponent& gm = meshes->Add(ground);
                gm.mesh = geo::Primitives::Plane(500.0f, 500.0f);
                gm.material = mat::MaterialBuilder(u8"stress.ground").Shader(u8"forward")
                    .Color(u8"BaseColor", rc::Vec4{ 0.3f, 0.3f, 0.3f, 1.0f })
                    .Float(u8"Metallic", 0.0f).Float(u8"Roughness", 0.8f).Build();
            }

            // Directional key light.
            if (auto* lights = m_scene->GetSystem<rd::LightComponentManager>()) {
                sc::EntityHandle sun = m_scene->CreateEntity(u8"sun");
                rc::Transform st = m_scene->GetLocalTransform(sun);
                st.rotation = rc::Quat::FromAxisAngle(rc::Vec3{ 1.0f, 0.0f, 0.0f }, -0.9f)
                            * rc::Quat::FromAxisAngle(rc::Vec3{ 0.0f, 1.0f, 0.0f }, 0.5f);
                m_scene->SetLocalTransform(sun, st);
                rd::LightComponent& sl = lights->Add(sun);
                sl.type      = rd::LightType::Directional;
                sl.color     = rc::Color{ 1.0f, 0.95f, 0.9f, 1.0f };
                sl.intensity = 1.5f;
            }

            // Fly camera, pulled well back + up so the whole grid is in frame (worst case for culling).
            m_camera = m_scene->CreateEntity(u8"camera");
            if (auto* cameras = m_scene->GetSystem<rd::CameraComponentManager>()) {
                rd::CameraComponent& cam = cameras->Add(m_camera);
                cam.fovYRadians = 1.04719755f;   // 60 deg
                cam.nearZ       = 0.1f;
                cam.farZ        = 2000.0f;
                cam.clearColor  = rc::Color{ 0.04f, 0.05f, 0.07f, 1.0f };
            }
            UpdateCameraTransform();

            AddSphereBatch();   // start with one batch

            rc::ConsoleWrite(u8"=== Render Stress Test ===\n"
                             u8"  Space: +8000 spheres   Backspace: -8000\n"
                             u8"  U: toggle unique materials (defeats batching)\n"
                             u8"  B: toggle sin-wave bob (defeats static caching)\n"
                             u8"  H: toggle console stats   P: profiler dump\n"
                             u8"  WASD/QE move, RMB look, Tab capture, Shift fast, Esc exit\n"
                             u8"==========================\n");
        }

        // The default render path reads the camera's aspect straight from the component, so keep it in
        // sync with the backbuffer before delegating to DefaultApplication's single-view render.
        void OnRenderWindow(rt::IApplicationHost& host, rt::FrameContext& frame) override
        {
            if (m_scene != nullptr && frame.height > 0) {
                if (auto* cameras = m_scene->GetSystem<rd::CameraComponentManager>()) {
                    if (rd::CameraComponent* cam = cameras->Get(m_camera)) {
                        cam->aspect = static_cast<rc::f32>(frame.width) / static_cast<rc::f32>(frame.height);
                    }
                }
            }
            rt::DefaultApplication::OnRenderWindow(host, frame);
        }

        void OnUpdate(rt::IApplicationHost& host, rc::f32 deltaTime) override
        {
            rt::DefaultApplication::OnUpdate(host, deltaTime);   // inherited P-key profiler dump
            if (m_scene == nullptr) { return; }

            auto* input = host.Platform() != nullptr ? host.Platform()->Input() : nullptr;
            rt::IKeyboard* kb = input != nullptr ? input->Keyboard() : nullptr;
            rt::IMouse*    mouse = input != nullptr ? input->Mouse() : nullptr;
            if (kb == nullptr) { return; }

            if (kb->IsKeyPressed(rt::KeyCode::Escape)) { host.RequestExit(0); return; }

            // --- load controls ---
            if (kb->IsKeyPressed(rt::KeyCode::Space))     { AddSphereBatch(); }
            if (kb->IsKeyPressed(rt::KeyCode::Backspace)) { RemoveLastBatch(); }
            if (kb->IsKeyPressed(rt::KeyCode::U)) {
                m_uniqueMaterials = !m_uniqueMaterials;
                RebuildSphereMaterials();
                rc::ConsoleWrite(m_uniqueMaterials ? u8"Unique materials: ON (a draw per sphere)\n"
                                                   : u8"Unique materials: OFF (shared, batched)\n");
            }
            if (kb->IsKeyPressed(rt::KeyCode::B)) {
                m_bob = !m_bob;
                rc::ConsoleWrite(m_bob ? u8"Sin-wave bob: ON (transforms rewritten every frame)\n"
                                       : u8"Sin-wave bob: OFF\n");
            }
            if (kb->IsKeyPressed(rt::KeyCode::H)) { m_showStats = !m_showStats; }

            UpdateCamera(deltaTime, *kb, mouse);

            // --- sin-wave bob: rewrite every sphere's Y each frame (no static optimization possible) ---
            m_time += deltaTime;
            if (m_bob) {
                auto* meshScene = m_scene;
                constexpr rc::f32 amplitude = 1.0f, speed = 2.0f;
                for (sc::EntityHandle e : m_spheres) {
                    rc::Transform t = meshScene->GetLocalTransform(e);
                    // Phase from world X/Z (stable as the grid grows), lifted so min sits on the plane.
                    const rc::f32 phase = (t.position.x + t.position.z) * 0.2f;
                    t.position.y = rc::Sin(m_time * speed + phase) * amplitude + amplitude;
                    meshScene->SetLocalTransform(e, t);
                }
            }

            UpdateStats(deltaTime);
        }

        void OnShutdown(rt::IApplicationHost&) override
        {
            rc::ConsoleWrite(u8"=== Stress Test Shutdown ===\n");
        }

    private:
        // Compose the camera entity's transform from the fly-cam yaw/pitch + position. Default camera
        // forward is -Z; yaw rotates about world Y, pitch about local X.
        void UpdateCameraTransform()
        {
            const rc::Quat rot = rc::Quat::FromAxisAngle(rc::Vec3{ 0.0f, 1.0f, 0.0f }, m_yaw)
                               * rc::Quat::FromAxisAngle(rc::Vec3{ 1.0f, 0.0f, 0.0f }, m_pitch);
            rc::Transform t = m_scene->GetLocalTransform(m_camera);
            t.position = m_camPos;
            t.rotation = rot;
            m_scene->SetLocalTransform(m_camera, t);
        }

        void UpdateCamera(rc::f32 dt, rt::IKeyboard& kb, rt::IMouse* mouse)
        {
            if (mouse != nullptr) {
                if (kb.IsKeyPressed(rt::KeyCode::Tab)) {
                    m_mouseCaptured = !m_mouseCaptured;
                    mouse->SetRelativeMode(m_mouseCaptured);
                    mouse->SetCursorVisible(!m_mouseCaptured);
                }
                if (m_mouseCaptured || mouse->IsButtonDown(rt::MouseButton::Right)) {
                    m_yaw   -= mouse->DeltaX() * kLookSens;
                    m_pitch -= mouse->DeltaY() * kLookSens;
                    m_pitch  = rc::Clamp(m_pitch, -1.55f, 1.55f);
                }
            }

            const rc::Quat rot = rc::Quat::FromAxisAngle(rc::Vec3{ 0.0f, 1.0f, 0.0f }, m_yaw)
                               * rc::Quat::FromAxisAngle(rc::Vec3{ 1.0f, 0.0f, 0.0f }, m_pitch);
            const rc::Vec3 forward = rc::RotateVector(rot, rc::Vec3{ 0.0f, 0.0f, -1.0f });
            const rc::Vec3 right   = rc::RotateVector(rot, rc::Vec3{ 1.0f, 0.0f, 0.0f });
            const rc::f32  speed   = (kb.IsKeyDown(rt::KeyCode::LeftShift) ? 200.0f : 50.0f) * dt;

            rc::Vec3 move{ 0.0f, 0.0f, 0.0f };
            if (kb.IsKeyDown(rt::KeyCode::W)) { move = move + forward; }
            if (kb.IsKeyDown(rt::KeyCode::S)) { move = move - forward; }
            if (kb.IsKeyDown(rt::KeyCode::D)) { move = move + right; }
            if (kb.IsKeyDown(rt::KeyCode::A)) { move = move - right; }
            if (kb.IsKeyDown(rt::KeyCode::E)) { move = move + rc::Vec3{ 0.0f, 1.0f, 0.0f }; }
            if (kb.IsKeyDown(rt::KeyCode::Q)) { move = move - rc::Vec3{ 0.0f, 1.0f, 0.0f }; }
            if (rc::Dot(move, move) > 0.0f) { m_camPos = m_camPos + rc::Normalized(move) * speed; }

            UpdateCameraTransform();
        }

        // Spawn 8000 more spheres on the auto-sized grid. Position only depends on a global index, so
        // existing spheres keep their world positions when the grid widens.
        void AddSphereBatch()
        {
            auto* meshes = m_scene->GetSystem<rd::MeshComponentManager>();
            if (meshes == nullptr) { return; }

            const rc::i32 startIndex = m_batchCount * kSpheresPerBatch;
            const rc::i32 newTotal   = (m_batchCount + 1) * kSpheresPerBatch;
            m_gridSize = static_cast<rc::i32>(rc::Ceil(rc::Sqrt(static_cast<rc::f32>(newTotal))));

            for (rc::i32 i = 0; i < kSpheresPerBatch; ++i) {
                const rc::i32 index = startIndex + i;
                const rc::i32 gx = index % m_gridSize;
                const rc::i32 gz = index / m_gridSize;
                const rc::f32 x = (static_cast<rc::f32>(gx) - static_cast<rc::f32>(m_gridSize) * 0.5f) * kSphereSpacing;
                const rc::f32 z = (static_cast<rc::f32>(gz) - static_cast<rc::f32>(m_gridSize) * 0.5f) * kSphereSpacing;

                sc::EntityHandle e = m_scene->CreateEntity(u8"sphere");
                m_scene->SetLocalPosition(e, rc::Vec3{ x, 0.5f, z });
                rd::MeshComponent& mc = meshes->Add(e);
                mc.mesh = m_sphere;
                AssignSphereMaterial(mc, index);
                m_spheres.PushBack(e);
            }

            ++m_batchCount;
            PrintCounts();
        }

        void RemoveLastBatch()
        {
            if (m_batchCount <= 0) { return; }
            rc::i32 removeCount = kSpheresPerBatch;
            if (static_cast<rc::usize>(removeCount) > m_spheres.Size()) {
                removeCount = static_cast<rc::i32>(m_spheres.Size());
            }
            for (rc::i32 i = 0; i < removeCount; ++i) {
                m_scene->DestroyEntity(m_spheres[m_spheres.Size() - 1]);
                m_spheres.PopBack();
                if (m_uniqueMaterials && !m_uniqueMats.IsEmpty()) { m_uniqueMats.PopBack(); }
            }
            --m_batchCount;
            PrintCounts();
        }

        // Point a sphere's mesh component at the right material for the current mode.
        void AssignSphereMaterial(rd::MeshComponent& mc, rc::i32 index)
        {
            if (m_uniqueMaterials) {
                const rc::f32 hue = static_cast<rc::f32>(index % 360) / 360.0f;
                const rc::Vec3 c = HsvToRgb(hue, 0.8f, 0.9f);
                rc::RefPtr<mat::Material> m = mat::MaterialBuilder(u8"stress.unique").Shader(u8"forward")
                    .Color(u8"BaseColor", rc::Vec4{ c.x, c.y, c.z, 1.0f })
                    .Float(u8"Metallic", 0.1f).Float(u8"Roughness", 0.4f).Build();
                mc.material = m;
                mc.color    = rc::Color{ 1.0f, 1.0f, 1.0f, 1.0f };
                m_uniqueMats.PushBack(static_cast<rc::RefPtr<mat::Material>&&>(m));
            } else {
                mc.material = m_sharedMat;
                mc.color    = rc::Color{ 1.0f, 1.0f, 1.0f, 1.0f };
            }
        }

        // Toggling unique mode re-points every existing sphere. Rebuild from scratch so material count
        // tracks the mode exactly (in shared mode we drop all the unique materials).
        void RebuildSphereMaterials()
        {
            auto* meshes = m_scene->GetSystem<rd::MeshComponentManager>();
            if (meshes == nullptr) { return; }
            m_uniqueMats.Clear();
            for (rc::usize i = 0; i < m_spheres.Size(); ++i) {
                if (rd::MeshComponent* mc = meshes->Get(m_spheres[i])) {
                    AssignSphereMaterial(*mc, static_cast<rc::i32>(i));
                }
            }
        }

        void PrintCounts()
        {
            rc::String s;
            rc::AppendFormat(s, u8"  spheres {}  batches {}  grid {}x{}  materials {}\n",
                             static_cast<rc::i32>(m_spheres.Size()), m_batchCount, m_gridSize, m_gridSize,
                             m_uniqueMaterials ? static_cast<rc::i32>(m_uniqueMats.Size()) : 1);
            rc::ConsoleWrite(s.AsView());
        }

        // Smooth the frame time and emit a one-line console stat once per second (when enabled).
        void UpdateStats(rc::f32 dt)
        {
            m_frameMs = m_frameMs * 0.9f + (dt * 1000.0f) * 0.1f;
            m_statsTimer += dt;
            if (!m_showStats || m_statsTimer < 1.0f) { return; }
            m_statsTimer = 0.0f;

            const rc::f32 fps = m_frameMs > 0.001f ? 1000.0f / m_frameMs : 0.0f;
            const rc::i32 msWhole = static_cast<rc::i32>(m_frameMs);
            const rc::i32 msTenth = static_cast<rc::i32>((m_frameMs - static_cast<rc::f32>(msWhole)) * 10.0f);

            rc::String s;
            rc::AppendFormat(s, u8"[stress] {} fps  {}.{} ms  | spheres {}  draws~{}  bob {}\n",
                             static_cast<rc::i32>(fps + 0.5f), msWhole, msTenth,
                             static_cast<rc::i32>(m_spheres.Size()),
                             m_uniqueMaterials ? static_cast<rc::i32>(m_spheres.Size()) : 1,
                             m_bob ? u8"on" : u8"off");
            rc::ConsoleWrite(s.AsView());
        }

        static rc::Vec3 HsvToRgb(rc::f32 h, rc::f32 s, rc::f32 v)
        {
            const rc::i32 i = static_cast<rc::i32>(h * 6.0f);
            const rc::f32 f = h * 6.0f - static_cast<rc::f32>(i);
            const rc::f32 p = v * (1.0f - s);
            const rc::f32 q = v * (1.0f - f * s);
            const rc::f32 t = v * (1.0f - (1.0f - f) * s);
            switch (i % 6) {
                case 0:  return rc::Vec3{ v, t, p };
                case 1:  return rc::Vec3{ q, v, p };
                case 2:  return rc::Vec3{ p, v, t };
                case 3:  return rc::Vec3{ p, q, v };
                case 4:  return rc::Vec3{ t, p, v };
                default: return rc::Vec3{ v, p, q };
            }
        }

        sc::Scene*                       m_scene = nullptr;
        sc::EntityHandle                 m_camera{};
        rc::RefPtr<geo::StaticMesh>      m_sphere;
        rc::RefPtr<mat::Material>        m_sharedMat;
        rc::Array<sc::EntityHandle>      m_spheres;
        rc::Array<rc::RefPtr<mat::Material>> m_uniqueMats;

        rc::i32 m_batchCount = 0;
        rc::i32 m_gridSize   = 0;
        bool    m_uniqueMaterials = false;
        bool    m_bob = false;
        rc::f32 m_time = 0.0f;

        // Fly camera
        rc::Vec3 m_camPos{ 0.0f, 50.0f, 200.0f };
        rc::f32  m_yaw   = 0.0f;     // 0 => looking down -Z (toward the grid at the origin)
        rc::f32  m_pitch = -0.245f;  // tilted down to take in the grid
        bool     m_mouseCaptured = false;

        // Stats
        bool    m_showStats  = true;
        rc::f32 m_frameMs    = 0.0f;
        rc::f32 m_statsTimer = 0.0f;
    };
}

int main(int, char**)
{
    auto platform = rt::CreatePlatform();
    rt::GraphicsDeviceDesc gpuDesc{};
    auto gpu = rt::CreateGraphicsDevice(gpuDesc);
    rt::GraphicsDevice* device = gpu.HasValue() ? gpu.Value().Get() : nullptr;

    StressTestApp app;
    return rt::RunApplication(app, *platform, device);
}
