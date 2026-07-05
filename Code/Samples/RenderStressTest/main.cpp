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
import draconic.shell;
import draconic.runtime.desktop;
import draconic.shell.desktop;
import draconic.graphics;
import draconic.graphics.gpu;
import draconic.runtime.defaultapp;
import draconic.scene;
import draconic.scene.subsystem;
import draconic.render.subsystem;
import draconic.imgui;                    // ImguiSubsystem (HUD)
import draconic.geometry;
import draconic.materials;

#include "../Common/FlyCamera.h"   // shared free-fly camera (uses the imported runtime/core types)

namespace core  = draconic::core;
namespace rhi = draconic::rhi;
namespace runtime  = draconic::runtime;
namespace graphics = draconic::graphics;
namespace shell  = draconic::shell;
namespace samples = draconic::samples;
namespace scene  = draconic::scene;
namespace render  = draconic::render;
namespace imgui = draconic::imgui;
namespace geometry = draconic::geometry;
namespace materials = draconic::materials;

namespace
{
    class StressTestApp final : public runtime::DefaultApplication
    {
        static constexpr core::i32 kSpheresPerBatch = 8000;
        static constexpr core::f32 kSphereSpacing   = 1.5f;
        static constexpr core::f32 kSphereHeight    = 2.5f;   // base height above the floor (radius 0.5)
        static constexpr core::f32 kFloorBaseSize   = 500.0f; // base ground-plane size (scaled to cover the grid)

    public:
        // Run uncapped (vsync off) so the frame time reflects real CPU+GPU work, not the display
        // refresh. The numbers tear visually — that's fine for a benchmark. Switch to Fifo to cap.
        graphics::RenderWindowDesc MainRenderWindow() const override
        {
            graphics::RenderWindowDesc d;
            d.presentMode = rhi::PresentMode::Immediate;
            return d;
        }

        // Register the ImGui subsystem so the benchmark HUD can draw over the scene.
        void Configure(runtime::IApplicationHost& host) override
        {
            runtime::DefaultApplication::Configure(host);
            if (auto* gfx = host.Graphics(); gfx != nullptr && gfx->Raw() != nullptr) {
                host.Ctx().AddSubsystem<imgui::ImguiSubsystem>(*gfx->Raw(), gfx->FramesInFlight());
            }
        }

        void OnStartup(runtime::IApplicationHost& host) override
        {
            auto* scenes = host.Ctx().GetSubsystem<scene::SceneSubsystem>();
            if (scenes == nullptr) { return; }
            m_scene = scenes->CreateScene(u8"stress");

            // A modest ambient so unlit-facing hemispheres aren't pure black.
            if (auto* env = m_scene->GetSystem<render::EnvironmentSystem>()) {
                env->Environment().ambientColor     = core::Color{ 0.10f, 0.12f, 0.16f, 1.0f };
                env->Environment().ambientIntensity = 0.30f;
            }

            // Shared sphere material (gray PBR). Every sphere points at THIS one by default, so the
            // renderer can batch them. Unique mode (U) builds a per-sphere material instead.
            m_sharedMat = materials::CreatePBR(u8"stress.shared", core::Vector4{ 0.7f, 0.7f, 0.7f, 1.0f }, 0.1f, 0.4f);

            // One sphere mesh, shared by all instances (matches Sedulous: radius 0.5, 16x8).
            m_sphere = geometry::Primitives::Sphere(0.5f, 16, 8);

            // Large ground plane so the bobbing spheres read against a surface.
            if (auto* meshes = m_scene->GetSystem<render::MeshComponentManager>()) {
                m_ground = m_scene->CreateEntity(u8"ground");
                m_scene->SetLocalPosition(m_ground, core::Vector3{ 0.0f, 0.0f, 0.0f });
                render::MeshComponent& gm = meshes->Add(m_ground);
                gm.mesh = geometry::Primitives::Plane(kFloorBaseSize, kFloorBaseSize);
                gm.material = materials::CreatePBR(u8"stress.ground", core::Vector4{ 0.3f, 0.3f, 0.3f, 1.0f }, 0.0f, 0.8f);
            }

            // Directional key light.
            if (auto* lights = m_scene->GetSystem<render::LightComponentManager>()) {
                scene::EntityHandle sun = m_scene->CreateEntity(u8"sun");
                core::Transform st = m_scene->GetLocalTransform(sun);
                st.rotation = core::Quaternion::FromAxisAngle(core::Vector3{ 1.0f, 0.0f, 0.0f }, -0.9f)
                            * core::Quaternion::FromAxisAngle(core::Vector3{ 0.0f, 1.0f, 0.0f }, 0.5f);
                m_scene->SetLocalTransform(sun, st);
                render::LightComponent& sl = lights->Add(sun);
                sl.type         = render::LightType::Directional;
                sl.color        = core::Color{ 1.0f, 0.95f, 0.9f, 1.0f };
                sl.intensity    = 1.5f;
                sl.castsShadows = true;   // phase 5.1: the spheres cast shadows on the ground
                m_sun = sun;              // K toggles its shadows (for shadowed-vs-unshadowed benchmarking)
            }

            // Fly camera, pulled well back + up so the whole grid is in frame (worst case for culling).
            m_camera = m_scene->CreateEntity(u8"camera");
            if (auto* cameras = m_scene->GetSystem<render::CameraComponentManager>()) {
                render::CameraComponent& cam = cameras->Add(m_camera);
                cam.fovYRadians = 1.04719755f;   // 60 deg
                cam.nearZ       = 0.1f;
                cam.farZ        = 2000.0f;
                cam.clearColor  = core::Color{ 0.04f, 0.05f, 0.07f, 1.0f };
            }
            PushCameraToEntity();

            AddSphereBatch();   // start with one batch

            // Lower default exposure: the procedural-sky IBL + sun are bright, so AgX washes out at 1.0.
            if (auto* render = host.Ctx().GetSubsystem<render::RenderSubsystem>()) { render->SetExposure(0.5f); }

            core::ConsoleWrite(u8"=== Render Stress Test ===\n"
                             u8"  Space: +8000 spheres   Backspace: -8000\n"
                             u8"  U: toggle unique materials (defeats batching)\n"
                             u8"  B: toggle sin-wave bob (defeats static caching)\n"
                             u8"  H: toggle console stats   P: profiler dump\n"
                             u8"  WASD/QE move, RMB look, Tab capture, Shift fast, Esc exit\n"
                             u8"==========================\n");
        }

        // The default render path reads the camera's aspect straight from the component, so keep it in
        // sync with the backbuffer before delegating to DefaultApplication's single-view render.
        void OnRenderWindow(runtime::IApplicationHost& host, graphics::FrameContext& frame) override
        {
            if (m_scene != nullptr && frame.height > 0) {
                if (auto* cameras = m_scene->GetSystem<render::CameraComponentManager>()) {
                    if (render::CameraComponent* cam = cameras->Get(m_camera)) {
                        cam->aspect = static_cast<core::f32>(frame.width) / static_cast<core::f32>(frame.height);
                    }
                }
            }
            runtime::DefaultApplication::OnRenderWindow(host, frame);

            // HUD over the scene (backbuffer is RenderTarget after the default render path).
            if (auto* g = host.Ctx().GetSubsystem<imgui::ImguiSubsystem>()) { g->Render(frame); }
        }

        void OnUpdate(runtime::IApplicationHost& host, core::f32 deltaTime) override
        {
            runtime::DefaultApplication::OnUpdate(host, deltaTime);   // inherited P-key profiler dump

            // Smooth the frame time every frame + build the ImGui HUD (drawn in OnRenderWindow).
            m_frameMs = m_frameMs * 0.9f + (deltaTime * 1000.0f) * 0.1f;
            if (auto* g = host.Ctx().GetSubsystem<imgui::ImguiSubsystem>()) {
                g->NewFrame(host.Shell() != nullptr ? host.Shell()->Input() : nullptr, deltaTime);
                BuildHud(host.Ctx().GetSubsystem<render::RenderSubsystem>());
            }
            if (m_scene == nullptr) { return; }

            auto* input = host.Shell() != nullptr ? host.Shell()->Input() : nullptr;
            shell::IKeyboard* kb = input != nullptr ? input->Keyboard() : nullptr;
            if (kb == nullptr) { return; }   // mouse-look is handled inside m_fly.Update

            if (kb->IsKeyPressed(shell::KeyCode::Escape)) { host.RequestExit(0); return; }

            // --- load controls ---
            if (kb->IsKeyPressed(shell::KeyCode::Space))     { AddSphereBatch(); }
            if (kb->IsKeyPressed(shell::KeyCode::Backspace)) { RemoveLastBatch(); }
            if (kb->IsKeyPressed(shell::KeyCode::U)) {
                m_uniqueMaterials = !m_uniqueMaterials;
                RebuildSphereMaterials();
                core::ConsoleWrite(m_uniqueMaterials ? u8"Unique materials: ON (a draw per sphere)\n"
                                                   : u8"Unique materials: OFF (shared, batched)\n");
            }
            if (kb->IsKeyPressed(shell::KeyCode::B)) {
                m_bob = !m_bob;
                core::ConsoleWrite(m_bob ? u8"Sin-wave bob: ON (transforms rewritten every frame)\n"
                                       : u8"Sin-wave bob: OFF\n");
            }
            if (kb->IsKeyPressed(shell::KeyCode::H)) { m_showStats = !m_showStats; }
            if (kb->IsKeyPressed(shell::KeyCode::T)) {   // toggle TAA (activates per-instance motion-vector prev-world path)
                if (auto* render = host.Ctx().GetSubsystem<render::RenderSubsystem>()) {
                    const bool on = !render->TaaEnabled();
                    render->SetTaaEnabled(on);
                    core::ConsoleWrite(on ? u8"TAA: ON (motion vectors active)\n" : u8"TAA: OFF\n");
                }
            }
            if (kb->IsKeyPressed(shell::KeyCode::I)) {   // toggle prepass->forward instance-data sharing (A/B regression/perf)
                if (auto* render = host.Ctx().GetSubsystem<render::RenderSubsystem>()) {
                    const bool on = !render->InstanceSharing();
                    render->SetInstanceSharing(on);
                    core::ConsoleWrite(on ? u8"Instance sharing: ON (prepass builds once, forward reuses)\n"
                                        : u8"Instance sharing: OFF (forward re-fills = old double-build)\n");
                }
            }
            if (kb->IsKeyPressed(shell::KeyCode::K)) {   // toggle directional shadows (Sedulous's 104k demo runs shadow-OFF)
                if (auto* lights = m_scene->GetSystem<render::LightComponentManager>()) {
                    if (render::LightComponent* sl = m_sun.IsAssigned() ? lights->Get(m_sun) : nullptr) {
                        sl->castsShadows = !sl->castsShadows;
                        core::ConsoleWrite(sl->castsShadows ? u8"Directional shadows: ON (CSM)\n"
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
                constexpr core::f32 amplitude = 1.0f, speed = 2.0f;
                for (scene::EntityHandle e : m_spheres) {
                    core::Transform t = meshScene->GetLocalTransform(e);
                    // Phase from world X/Z (stable as the grid grows). Bob AROUND the base height so the
                    // spheres stay above the floor (full, separated shadows) instead of dipping through it.
                    const core::f32 phase = (t.position.x + t.position.z) * 0.2f;
                    t.position.y = kSphereHeight + core::Sin(m_time * speed + phase) * amplitude;
                    meshScene->SetLocalTransform(e, t);
                }
            }
        }

        void OnShutdown(runtime::IApplicationHost&) override
        {
            core::ConsoleWrite(u8"=== Stress Test Shutdown ===\n");
        }

    private:
        // Push the fly camera's pose onto the camera entity (the default render path reads it).
        void PushCameraToEntity()
        {
            core::Transform t = m_scene->GetLocalTransform(m_camera);
            t.position = m_fly.position;
            t.rotation = m_fly.Rotation();
            m_scene->SetLocalTransform(m_camera, t);
        }

        // Grow the ground plane to cover the current grid, and re-frame the fly camera so the whole grid
        // is in view (called on every batch change) — like AnimStressTest. Keeps the ground under the
        // whole field and the far plane wide enough that no spheres get frustum-far-culled.
        void FitFloorAndCamera()
        {
            const core::f32 gridWidth = static_cast<core::f32>(m_gridSize) * kSphereSpacing;   // full grid extent
            // Floor: scale the base plane so it covers the grid + a margin (uniform XZ; Y stays flat).
            const core::f32 scale = core::Max(0.1f, (gridWidth + 40.0f) / kFloorBaseSize);
            core::Transform ft = m_scene->GetLocalTransform(m_ground);
            ft.scale = core::Vector3{ scale, 1.0f, scale };
            m_scene->SetLocalTransform(m_ground, ft);

            // Camera: pull back + up so the grid fits the 60° FOV, looking down at the center.
            const core::f32 extent = gridWidth * 0.5f + 6.0f;
            const core::f32 dist   = extent / core::Tan(0.5236f) + 10.0f;   // half of 60° = 0.5236 rad
            const core::f32 camY   = extent * 0.55f + kSphereHeight;
            m_fly.position = core::Vector3{ 0.0f, kSphereHeight + camY, dist };
            m_fly.yaw      = 0.0f;
            m_fly.pitch    = -core::Atan2(camY, dist);   // look down onto the grid center
            if (auto* cameras = m_scene->GetSystem<render::CameraComponentManager>()) {
                if (render::CameraComponent* cam = cameras->Get(m_camera)) {
                    cam->farZ = dist + extent * 2.0f + 200.0f;   // cover the grid; don't far-cull spheres
                }
            }
            PushCameraToEntity();
        }

        // Spawn 8000 more spheres on the auto-sized grid. Position only depends on a global index, so
        // existing spheres keep their world positions when the grid widens.
        void AddSphereBatch()
        {
            auto* meshes = m_scene->GetSystem<render::MeshComponentManager>();
            if (meshes == nullptr) { return; }

            const core::i32 startIndex = m_batchCount * kSpheresPerBatch;
            const core::i32 newTotal   = (m_batchCount + 1) * kSpheresPerBatch;
            m_gridSize = static_cast<core::i32>(core::Ceil(core::Sqrt(static_cast<core::f32>(newTotal))));

            for (core::i32 i = 0; i < kSpheresPerBatch; ++i) {
                const core::i32 index = startIndex + i;
                const core::i32 gx = index % m_gridSize;
                const core::i32 gz = index / m_gridSize;
                const core::f32 x = (static_cast<core::f32>(gx) - static_cast<core::f32>(m_gridSize) * 0.5f) * kSphereSpacing;
                const core::f32 z = (static_cast<core::f32>(gz) - static_cast<core::f32>(m_gridSize) * 0.5f) * kSphereSpacing;

                scene::EntityHandle e = m_scene->CreateEntity(u8"sphere");
                m_scene->SetLocalPosition(e, core::Vector3{ x, kSphereHeight, z });
                render::MeshComponent& mc = meshes->Add(e);
                mc.mesh = m_sphere;
                AssignSphereMaterial(mc, index);
                m_spheres.PushBack(e);
            }

            ++m_batchCount;
            FitFloorAndCamera();
            PrintCounts();
        }

        void RemoveLastBatch()
        {
            if (m_batchCount <= 0) { return; }
            core::i32 removeCount = kSpheresPerBatch;
            if (static_cast<core::usize>(removeCount) > m_spheres.Size()) {
                removeCount = static_cast<core::i32>(m_spheres.Size());
            }
            for (core::i32 i = 0; i < removeCount; ++i) {
                m_scene->DestroyEntity(m_spheres[m_spheres.Size() - 1]);
                m_spheres.PopBack();
                if (m_uniqueMaterials && !m_uniqueMats.IsEmpty()) { m_uniqueMats.PopBack(); }
            }
            --m_batchCount;
            FitFloorAndCamera();
            PrintCounts();
        }

        // Point a sphere's mesh component at the right material for the current mode.
        void AssignSphereMaterial(render::MeshComponent& mc, core::i32 index)
        {
            if (m_uniqueMaterials) {
                const core::f32 hue = static_cast<core::f32>(index % 360) / 360.0f;
                const core::Vector3 c = HsvToRgb(hue, 0.8f, 0.9f);
                core::RefPtr<materials::Material> m = materials::CreatePBR(u8"stress.unique", core::Vector4{ c.x, c.y, c.z, 1.0f }, 0.1f, 0.4f);
                mc.material = m;
                mc.color    = core::Color{ 1.0f, 1.0f, 1.0f, 1.0f };
                m_uniqueMats.PushBack(static_cast<core::RefPtr<materials::Material>&&>(m));
            } else {
                mc.material = m_sharedMat;
                mc.color    = core::Color{ 1.0f, 1.0f, 1.0f, 1.0f };
            }
        }

        // Toggling unique mode re-points every existing sphere. Rebuild from scratch so material count
        // tracks the mode exactly (in shared mode we drop all the unique materials).
        void RebuildSphereMaterials()
        {
            auto* meshes = m_scene->GetSystem<render::MeshComponentManager>();
            if (meshes == nullptr) { return; }
            m_uniqueMats.Clear();
            for (core::usize i = 0; i < m_spheres.Size(); ++i) {
                if (render::MeshComponent* mc = meshes->Get(m_spheres[i])) {
                    AssignSphereMaterial(*mc, static_cast<core::i32>(i));
                }
            }
        }

        void PrintCounts()
        {
            core::String s;
            core::AppendFormat(s, u8"  spheres {}  batches {}  grid {}x{}  materials {}\n",
                             static_cast<core::i32>(m_spheres.Size()), m_batchCount, m_gridSize, m_gridSize,
                             m_uniqueMaterials ? static_cast<core::i32>(m_uniqueMats.Size()) : 1);
            core::ConsoleWrite(s.AsView());
        }

        // ImGui HUD: benchmark stats + controls (H toggles it). Built in OnUpdate, drawn in OnRenderWindow.
        void BuildHud(render::RenderSubsystem* render)
        {
            if (!m_showStats) { return; }
            ImGui::Begin("Render Stress Test");
            const float fps = m_frameMs > 0.001f ? 1000.0f / m_frameMs : 0.0f;
            ImGui::Text("%.0f fps   %.2f ms", static_cast<double>(fps), static_cast<double>(m_frameMs));
            ImGui::Text("spheres %d   batches %d   grid %dx%d",
                        static_cast<int>(m_spheres.Size()), m_batchCount, m_gridSize, m_gridSize);
            ImGui::Separator();
            if (ImGui::Button("+ batch (Space)")) { AddSphereBatch(); }
            ImGui::SameLine();
            if (ImGui::Button("- batch (Backspace)")) { RemoveLastBatch(); }
            // Scene toggles (also the hot-keys U/B/T/I/K).
            bool uniq = m_uniqueMaterials;
            if (ImGui::Checkbox("Unique materials (U)", &uniq)) { m_uniqueMaterials = uniq; RebuildSphereMaterials(); }
            ImGui::Checkbox("Sin-wave bob (B)", &m_bob);
            if (render != nullptr) {
                bool taa = render->TaaEnabled();
                if (ImGui::Checkbox("TAA (T)", &taa)) { render->SetTaaEnabled(taa); }
                bool inst = render->InstanceSharing();
                if (ImGui::Checkbox("Instance sharing (I)", &inst)) { render->SetInstanceSharing(inst); }
                bool cull = render->ViewCulling();
                if (ImGui::Checkbox("View-frustum cull", &cull)) { render->SetViewCulling(cull); }
                if (cull) {
                    core::u32 culled = 0, total = 0; render->ViewCullStats(culled, total);
                    ImGui::SameLine(); ImGui::TextDisabled("(%u/%u culled)", culled, total);
                }
            }
            if (m_scene != nullptr) {
                if (auto* lights = m_scene->GetSystem<render::LightComponentManager>()) {
                    if (render::LightComponent* sl = m_sun.IsAssigned() ? lights->Get(m_sun) : nullptr) {
                        bool sh = sl->castsShadows;
                        if (ImGui::Checkbox("Directional shadows (K)", &sh)) { sl->castsShadows = sh; }
                    }
                }
            }
            if (render != nullptr) {
                ImGui::Separator();
                float exposure = render->Exposure();
                if (ImGui::SliderFloat("Exposure", &exposure, 0.05f, 4.0f)) { render->SetExposure(exposure); }
                bool bloomOn = render->BloomEnabled();
                if (ImGui::Checkbox("Bloom", &bloomOn)) { render->SetBloomEnabled(bloomOn); }
                float shadowDist = render->ShadowDistance();
                if (ImGui::SliderFloat("Shadow dist", &shadowDist, 50.0f, 1000.0f, "%.0f")) { render->SetShadowDistance(shadowDist); }
                float shadowFade = render->ShadowFarFade();
                if (ImGui::SliderFloat("Shadow fade", &shadowFade, 2.0f, 150.0f, "%.0f")) { render->SetShadowFarFade(shadowFade); }
            }
            ImGui::Separator();
            ImGui::TextUnformatted("H hide HUD   P profiler");
            ImGui::TextUnformatted("WASD/QE move   RMB look   Shift fast   Esc exit");
            ImGui::End();
        }

        static core::Vector3 HsvToRgb(core::f32 h, core::f32 s, core::f32 v)
        {
            const core::i32 i = static_cast<core::i32>(h * 6.0f);
            const core::f32 f = h * 6.0f - static_cast<core::f32>(i);
            const core::f32 p = v * (1.0f - s);
            const core::f32 q = v * (1.0f - f * s);
            const core::f32 t = v * (1.0f - (1.0f - f) * s);
            switch (i % 6) {
                case 0:  return core::Vector3{ v, t, p };
                case 1:  return core::Vector3{ q, v, p };
                case 2:  return core::Vector3{ p, v, t };
                case 3:  return core::Vector3{ p, q, v };
                case 4:  return core::Vector3{ t, p, v };
                default: return core::Vector3{ v, p, q };
            }
        }

        scene::Scene*                       m_scene = nullptr;
        scene::EntityHandle                 m_camera{};
        scene::EntityHandle                 m_sun{};
        scene::EntityHandle                 m_ground{};
        core::RefPtr<geometry::StaticMesh>      m_sphere;
        core::RefPtr<materials::Material>        m_sharedMat;
        core::Array<scene::EntityHandle>      m_spheres;
        core::Array<core::RefPtr<materials::Material>> m_uniqueMats;

        core::i32 m_batchCount = 0;
        core::i32 m_gridSize   = 0;
        bool    m_uniqueMaterials = false;
        bool    m_bob = false;
        core::f32 m_time = 0.0f;

        // Fly camera, pulled well back + up so the whole grid is in frame (worst case for culling).
        samples::FlyCamera m_fly{ .position = core::Vector3{ 0.0f, 50.0f, 200.0f }, .pitch = -0.245f };

        // Stats
        bool    m_showStats  = true;   // HUD visibility (H)
        core::f32 m_frameMs    = 0.0f;
    };
}

int main(int, char**)
{
    auto shell = shell::CreateShell();
    graphics::GraphicsDeviceDesc gpuDesc{};
    auto gpu = graphics::CreateGraphicsDevice(gpuDesc);
    graphics::GraphicsDevice* device = gpu.HasValue() ? gpu.Value().Get() : nullptr;

    StressTestApp app;
    return runtime::RunApplication(app, *shell, device);
}
