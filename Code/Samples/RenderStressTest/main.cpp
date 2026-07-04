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
#include "imgui.h"   // Dear ImGui (HUD) — used directly; engine integration is draconic.imgui

import draconic.core;
import draconic.rhi;                     // PresentMode (run the benchmark vsync-off)
import draconic.runtime;
import draconic.runtime.client;
import draconic.runtime.platform;
import draconic.runtime.platform.desktop;
import draconic.runtime.graphics;
import draconic.runtime.graphics.gpu;
import draconic.runtime.defaultapp;
import draconic.scene;
import draconic.scene.subsystem;
import draconic.render.subsystem;
import draconic.imgui;                    // ImguiSubsystem (HUD)
import draconic.geometry;
import draconic.materials;

#include "../Common/FlyCamera.h"   // shared free-fly camera (uses the imported runtime/core types)

namespace rc  = draconic::core;
namespace rhi = draconic::rhi;
namespace rt  = draconic::runtime;
namespace smp = draconic::samples;
namespace sc  = draconic::scene;
namespace rd  = draconic::render;
namespace gui = draconic::imgui;
namespace geo = draconic::geometry;
namespace mat = draconic::materials;

namespace
{
    class StressTestApp final : public rt::DefaultApplication
    {
        static constexpr rc::i32 kSpheresPerBatch = 8000;
        static constexpr rc::f32 kSphereSpacing   = 1.5f;
        static constexpr rc::f32 kSphereHeight    = 2.5f;   // base height above the floor (radius 0.5)

    public:
        // Run uncapped (vsync off) so the frame time reflects real CPU+GPU work, not the display
        // refresh. The numbers tear visually — that's fine for a benchmark. Switch to Fifo to cap.
        rt::RenderWindowDesc MainRenderWindow() const override
        {
            rt::RenderWindowDesc d;
            d.presentMode = rhi::PresentMode::Immediate;
            return d;
        }

        // Register the ImGui subsystem so the benchmark HUD can draw over the scene.
        void Configure(rt::IApplicationHost& host) override
        {
            rt::DefaultApplication::Configure(host);
            if (auto* gfx = host.Graphics(); gfx != nullptr && gfx->Raw() != nullptr) {
                host.Ctx().AddSubsystem<gui::ImguiSubsystem>(*gfx->Raw(), gfx->FramesInFlight());
            }
        }

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
            m_sharedMat = mat::CreatePBR(u8"stress.shared", rc::Vec4{ 0.7f, 0.7f, 0.7f, 1.0f }, 0.1f, 0.4f);

            // One sphere mesh, shared by all instances (matches Sedulous: radius 0.5, 16x8).
            m_sphere = geo::Primitives::Sphere(0.5f, 16, 8);

            // Large ground plane so the bobbing spheres read against a surface.
            if (auto* meshes = m_scene->GetSystem<rd::MeshComponentManager>()) {
                sc::EntityHandle ground = m_scene->CreateEntity(u8"ground");
                m_scene->SetLocalPosition(ground, rc::Vec3{ 0.0f, 0.0f, 0.0f });
                rd::MeshComponent& gm = meshes->Add(ground);
                gm.mesh = geo::Primitives::Plane(500.0f, 500.0f);
                gm.material = mat::CreatePBR(u8"stress.ground", rc::Vec4{ 0.3f, 0.3f, 0.3f, 1.0f }, 0.0f, 0.8f);
            }

            // Directional key light.
            if (auto* lights = m_scene->GetSystem<rd::LightComponentManager>()) {
                sc::EntityHandle sun = m_scene->CreateEntity(u8"sun");
                rc::Transform st = m_scene->GetLocalTransform(sun);
                st.rotation = rc::Quat::FromAxisAngle(rc::Vec3{ 1.0f, 0.0f, 0.0f }, -0.9f)
                            * rc::Quat::FromAxisAngle(rc::Vec3{ 0.0f, 1.0f, 0.0f }, 0.5f);
                m_scene->SetLocalTransform(sun, st);
                rd::LightComponent& sl = lights->Add(sun);
                sl.type         = rd::LightType::Directional;
                sl.color        = rc::Color{ 1.0f, 0.95f, 0.9f, 1.0f };
                sl.intensity    = 1.5f;
                sl.castsShadows = true;   // phase 5.1: the spheres cast shadows on the ground
                m_sun = sun;              // K toggles its shadows (for shadowed-vs-unshadowed benchmarking)
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
            PushCameraToEntity();

            AddSphereBatch();   // start with one batch

            // Lower default exposure: the procedural-sky IBL + sun are bright, so AgX washes out at 1.0.
            if (auto* render = host.Ctx().GetSubsystem<rd::RenderSubsystem>()) { render->SetExposure(0.5f); }

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

            // HUD over the scene (backbuffer is RenderTarget after the default render path).
            if (auto* g = host.Ctx().GetSubsystem<gui::ImguiSubsystem>()) { g->Render(frame); }
        }

        void OnUpdate(rt::IApplicationHost& host, rc::f32 deltaTime) override
        {
            rt::DefaultApplication::OnUpdate(host, deltaTime);   // inherited P-key profiler dump

            // Smooth the frame time every frame + build the ImGui HUD (drawn in OnRenderWindow).
            m_frameMs = m_frameMs * 0.9f + (deltaTime * 1000.0f) * 0.1f;
            if (auto* g = host.Ctx().GetSubsystem<gui::ImguiSubsystem>()) {
                g->NewFrame(host.Platform() != nullptr ? host.Platform()->Input() : nullptr, deltaTime);
                BuildHud(host.Ctx().GetSubsystem<rd::RenderSubsystem>());
            }
            if (m_scene == nullptr) { return; }

            auto* input = host.Platform() != nullptr ? host.Platform()->Input() : nullptr;
            rt::IKeyboard* kb = input != nullptr ? input->Keyboard() : nullptr;
            if (kb == nullptr) { return; }   // mouse-look is handled inside m_fly.Update

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
            if (kb->IsKeyPressed(rt::KeyCode::T)) {   // toggle TAA (activates per-instance motion-vector prev-world path)
                if (auto* render = host.Ctx().GetSubsystem<rd::RenderSubsystem>()) {
                    const bool on = !render->TaaEnabled();
                    render->SetTaaEnabled(on);
                    rc::ConsoleWrite(on ? u8"TAA: ON (motion vectors active)\n" : u8"TAA: OFF\n");
                }
            }
            if (kb->IsKeyPressed(rt::KeyCode::K)) {   // toggle directional shadows (Sedulous's 104k demo runs shadow-OFF)
                if (auto* lights = m_scene->GetSystem<rd::LightComponentManager>()) {
                    if (rd::LightComponent* sl = m_sun.IsAssigned() ? lights->Get(m_sun) : nullptr) {
                        sl->castsShadows = !sl->castsShadows;
                        rc::ConsoleWrite(sl->castsShadows ? u8"Directional shadows: ON (CSM)\n"
                                                          : u8"Directional shadows: OFF (matches Sedulous stress test)\n");
                    }
                }
            }

            m_fly.Update(host, deltaTime);
            PushCameraToEntity();

            // --- sin-wave bob: rewrite every sphere's Y each frame (no static optimization possible) ---
            m_time += deltaTime;
            if (m_bob) {
                auto* meshScene = m_scene;
                constexpr rc::f32 amplitude = 1.0f, speed = 2.0f;
                for (sc::EntityHandle e : m_spheres) {
                    rc::Transform t = meshScene->GetLocalTransform(e);
                    // Phase from world X/Z (stable as the grid grows). Bob AROUND the base height so the
                    // spheres stay above the floor (full, separated shadows) instead of dipping through it.
                    const rc::f32 phase = (t.position.x + t.position.z) * 0.2f;
                    t.position.y = kSphereHeight + rc::Sin(m_time * speed + phase) * amplitude;
                    meshScene->SetLocalTransform(e, t);
                }
            }
        }

        void OnShutdown(rt::IApplicationHost&) override
        {
            rc::ConsoleWrite(u8"=== Stress Test Shutdown ===\n");
        }

    private:
        // Push the fly camera's pose onto the camera entity (the default render path reads it).
        void PushCameraToEntity()
        {
            rc::Transform t = m_scene->GetLocalTransform(m_camera);
            t.position = m_fly.position;
            t.rotation = m_fly.Rotation();
            m_scene->SetLocalTransform(m_camera, t);
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
                m_scene->SetLocalPosition(e, rc::Vec3{ x, kSphereHeight, z });
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
                rc::RefPtr<mat::Material> m = mat::CreatePBR(u8"stress.unique", rc::Vec4{ c.x, c.y, c.z, 1.0f }, 0.1f, 0.4f);
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

        // ImGui HUD: benchmark stats + controls (H toggles it). Built in OnUpdate, drawn in OnRenderWindow.
        void BuildHud(rd::RenderSubsystem* render)
        {
            if (!m_showStats) { return; }
            ImGui::Begin("Render Stress Test");
            const float fps = m_frameMs > 0.001f ? 1000.0f / m_frameMs : 0.0f;
            ImGui::Text("%.0f fps   %.2f ms", static_cast<double>(fps), static_cast<double>(m_frameMs));
            ImGui::Text("spheres %d   batches %d   grid %dx%d",
                        static_cast<int>(m_spheres.Size()), m_batchCount, m_gridSize, m_gridSize);
            ImGui::Text("materials: %s", m_uniqueMaterials ? "unique (a draw per sphere)" : "shared (batched)");
            ImGui::Text("bob: %s", m_bob ? "on" : "off");
            if (render != nullptr) {
                ImGui::Separator();
                float exposure = render->Exposure();
                if (ImGui::SliderFloat("Exposure", &exposure, 0.05f, 4.0f)) { render->SetExposure(exposure); }
                bool bloomOn = render->BloomEnabled();
                if (ImGui::Checkbox("Bloom", &bloomOn)) { render->SetBloomEnabled(bloomOn); }
            }
            ImGui::Separator();
            ImGui::TextUnformatted("Space +8000   Backspace -8000");
            ImGui::TextUnformatted("U unique mats   B bob   H hide HUD   P profiler");
            ImGui::TextUnformatted("WASD/QE move   RMB look   Shift fast   Esc exit");
            ImGui::End();
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
        sc::EntityHandle                 m_sun{};
        rc::RefPtr<geo::StaticMesh>      m_sphere;
        rc::RefPtr<mat::Material>        m_sharedMat;
        rc::Array<sc::EntityHandle>      m_spheres;
        rc::Array<rc::RefPtr<mat::Material>> m_uniqueMats;

        rc::i32 m_batchCount = 0;
        rc::i32 m_gridSize   = 0;
        bool    m_uniqueMaterials = false;
        bool    m_bob = false;
        rc::f32 m_time = 0.0f;

        // Fly camera, pulled well back + up so the whole grid is in frame (worst case for culling).
        smp::FlyCamera m_fly{ .position = rc::Vec3{ 0.0f, 50.0f, 200.0f }, .pitch = -0.245f };

        // Stats
        bool    m_showStats  = true;   // HUD visibility (H)
        rc::f32 m_frameMs    = 0.0f;
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
