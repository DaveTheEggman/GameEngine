// Sandbox — the running dev harness. It extends DefaultApplication (which registers
// the SceneSubsystem + RenderSubsystem and renders active scenes each frame), creates a
// scene with two spinning cube grids (instanced + distinct), and lets the engine draw it.
// As the renderer grows, this is where we exercise it.

#include "Core/Prelude.h"
#include "Profiler/Profiler.h"   // DRACONIC_PROFILE_SCOPE (isolate animation-drive cost)
#include "imgui.h"               // Dear ImGui (HUD) — used directly; integration is draconic.imgui

import draconic.core;
import draconic.profiler;
import draconic.rhi;                     // offscreen render target (Texture / ResourceState / Blit)
import draconic.runtime;
import draconic.runtime.client;
import draconic.runtime.platform;
import draconic.runtime.platform.desktop;
import draconic.runtime.graphics;
import draconic.runtime.graphics.gpu;
import draconic.runtime.defaultapp;     // DefaultApplication (scene + render subsystems)
import draconic.scene;
import draconic.scene.subsystem;
import draconic.render.subsystem;       // MeshComponent / CameraComponent + their managers
import draconic.render;                  // ViewCamera / ViewportRect (split-screen overrides)
import draconic.imgui;                    // ImguiSubsystem (HUD)
import draconic.geometry;
import draconic.geometry.resource;       // StaticMeshFactory + StaticMesh product
import draconic.materials;
import draconic.materials.resource;       // MaterialFactory (cooked materials)
import draconic.texture.resource;         // TextureFactory (cooked textures)
import draconic.animation.resource;       // Skeleton/AnimationClip factories
import draconic.vfs;                      // NativeFileSystem mount for the content DB
import draconic.content;                  // ContentDatabase (cooked-resource output)
import draconic.resource;                 // ResourceManager + Proxy
import draconic.model;                    // ModelLoadResult
import draconic.modelimporter;            // LoadAndCook + ImportedModel manifest
import draconic.animation;                // AnimationClip / Skeleton
import draconic.animation.subsystem;      // SkeletalAnimationComponent(Manager) — engine-driven skinning

#include "../Common/FlyCamera.h"   // shared free-fly camera (uses the imported runtime/core types)

#ifndef DRACONIC_SANDBOX_MODEL_DIR
#define DRACONIC_SANDBOX_MODEL_DIR ""
#endif
#ifndef DRACONIC_SANDBOX_OUTPUT_DIR
#define DRACONIC_SANDBOX_OUTPUT_DIR ""
#endif

namespace rc = draconic::core;
namespace smp = draconic::samples;
namespace rhi = draconic::rhi;
namespace rt = draconic::runtime;
namespace sc = draconic::scene;
namespace rd = draconic::render;
namespace gui = draconic::imgui;
namespace geo = draconic::geometry;
namespace mat = draconic::materials;
namespace tex = draconic::texture;
namespace vfs = draconic::vfs;
namespace ct  = draconic::content;
namespace res = draconic::resource;
namespace mdl  = draconic::model;
namespace mi   = draconic::modelimporter;
namespace anim = draconic::animation;

namespace
{
    // How many characters each +/- press adds or removes (and the initial spawn).
    static constexpr rc::u32 kBatchSize        = 25;
    static constexpr rc::f32 kCharacterSpacing = 8.0f;   // grid spacing (world units)
    static constexpr rc::f32 kCharacterSize    = 6.0f;   // auto-fit target height (matches CookModel)
    static constexpr rc::f32 kFloorY           = -7.0f;
    static constexpr rc::f32 kFloorBaseSize    = 120.0f; // base floor-plane size (scaled to cover the grid)

    class AnimStressTestApp final : public rt::DefaultApplication
    {
    public:
        // Run uncapped (vsync off) so the frame time reflects real CPU+GPU skinning work, not the
        // display refresh — same as RenderStressTest. The image tears; fine for a benchmark.
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

            // A large horizontal floor (Plane normal = +Y) under the scene — the animated models stand
            // on it and the lights cast their shadows onto it.
            if (auto* meshes = m_scene->GetSystem<rd::MeshComponentManager>()) {
                m_floor = m_scene->CreateEntity(u8"floor");
                m_scene->SetLocalPosition(m_floor, rc::Vec3{ 0.0f, -7.0f, 0.0f });
                rd::MeshComponent& fmc = meshes->Add(m_floor);
                fmc.mesh = geo::Primitives::Plane(kFloorBaseSize, kFloorBaseSize);
                fmc.material = mat::CreatePBR(u8"lit", rc::Vec4{ 0.5f, 0.5f, 0.53f, 1.0f }, 0.0f, 0.65f);
            }

            // One directional shadow-casting key light — the whole scene (skinning benchmark, kept light
            // to isolate skinning/animation cost, à la Flax's "5,000 basic characters" reference scene).
            if (auto* lights = m_scene->GetSystem<rd::LightComponentManager>()) {
                sc::EntityHandle key = m_scene->CreateEntity(u8"keyLight");
                rc::Transform kt = m_scene->GetLocalTransform(key);
                kt.rotation = rc::Quat::FromAxisAngle(rc::Vec3{ 1.0f, 0.0f, 0.0f }, -0.9f)
                            * rc::Quat::FromAxisAngle(rc::Vec3{ 0.0f, 1.0f, 0.0f }, 0.5f);
                m_scene->SetLocalTransform(key, kt);
                rd::LightComponent& kl = lights->Add(key);
                kl.type         = rd::LightType::Directional;
                kl.color        = rc::Color{ 1.0f, 0.97f, 0.92f, 1.0f };
                kl.intensity    = 2.5f;
                kl.castsShadows = true;   // directional CSM
            }

            LoadImportedModel(host);   // cook the character + spawn the initial grid

            // Lower default exposure: the procedural-sky IBL + sun are bright, so AgX washes out at 1.0.
            if (auto* render = host.Ctx().GetSubsystem<rd::RenderSubsystem>()) { render->SetExposure(0.5f); }

            rc::ConsoleWrite(u8"AnimStressTest: [Space] add batch  [Backspace] remove batch  [P] profiler  [Esc] exit\n");
        }

        // The model-import seam: open the cooked-resource output DB, register the geometry factory,
        // load+cook a glTF file through the importer, then spawn its node hierarchy as entities whose
        // MeshComponents reference the cooked StaticMesh resources. This is the clean runtime cook seam
        // the design calls for — an editor would cook offline and the runtime would only Bind, but the
        // wiring (factory -> Bind -> render) is identical.
        void LoadImportedModel(rt::IApplicationHost& host)
        {
            const rc::StringView outputDir(reinterpret_cast<const rc::utf8char*>(DRACONIC_SANDBOX_OUTPUT_DIR));
            const rc::StringView modelDir(reinterpret_cast<const rc::utf8char*>(DRACONIC_SANDBOX_MODEL_DIR));
            if (outputDir.IsEmpty() || modelDir.IsEmpty()) { return; }

            // Output DB (cooked resources) + resource manager + the factories. ModelFactory builds the
            // manifest into a ModelResource, resolving its meshes/materials/textures (dependency edges).
            m_contentFs = rc::MakeUnique<vfs::NativeFileSystem>(rc::DefaultAllocator(), outputDir);
            m_contentDb = rc::MakeUnique<ct::ContentDatabase>(rc::DefaultAllocator(), *m_contentFs);
            m_resources = rc::MakeUnique<res::ResourceManager>(rc::DefaultAllocator(), *m_contentDb);
            m_resources->AddFactory(&m_meshFactory);
            m_resources->AddFactory(&m_skinnedMeshFactory);
            m_resources->AddFactory(&m_modelFactory);
            m_resources->AddFactory(&m_materialFactory);
            m_resources->AddFactory(&m_skeletonFactory);
            m_resources->AddFactory(&m_clipFactory);
            if (auto* gfx = host.Graphics(); gfx != nullptr && gfx->Raw() != nullptr) {
                m_textureFactory = rc::MakeUnique<tex::TextureFactory>(rc::DefaultAllocator(), *gfx->Raw());
                m_resources->AddFactory(m_textureFactory.Get());
            }
            mi::RegisterModelImporterTypes();   // make the cooked types deserializable

            // Cook the Quaternius humanoid once, then replicate it across a grid (each instance gets its
            // own AnimationPlayer; all share the cooked mesh/skeleton/clips/materials).
            if (!CookModel(u8"Char", rc::Format(u8"{}/QuaterniusCharacter/glTF/Character.gltf", modelDir).AsView())) { return; }
            RebuildToCount(kAutoProfile ? kProfileCount : (kAutoRamp ? 100u : kBatchSize));
        }

        // Cook + bind + spawn one model, placed at `position` and auto-fit to a target size. Each model
        // spawns its node hierarchy (local TRS + parent links) under a scaled model-root entity; mesh
        // nodes get a MeshComponent referencing the cooked StaticMesh + material.
        // Cook + bind the model ONCE. Every spawned instance shares these resources (mesh/skeleton/
        // clips/materials); only the per-entity transform + AnimationPlayer differ. Returns true on success.
        bool CookModel(rc::StringView prefix, rc::StringView path)
        {
            if (m_contentDb.Get() == nullptr) { return false; }
            rc::Guid modelGuid;
            const mdl::ModelLoadResult r = mi::LoadAndCook(path, *m_contentDb, prefix, modelGuid);
            if (r != mdl::ModelLoadResult::Ok) {
                rc::ConsoleWrite(rc::Format(u8"AnimStressTest: model import failed ({})\n", static_cast<rc::u32>(r)));
                return false;
            }
            m_model = m_resources->Bind<mi::ModelResource>(modelGuid);
            if (!m_model) { rc::ConsoleWrite(u8"AnimStressTest: model bind failed\n"); return false; }

            // Auto-fit: scale the model's largest extent to a target size.
            constexpr rc::f32 kTargetSize = 6.0f;
            const rc::Vec3 extent = m_model->boundsMax - m_model->boundsMin;
            const rc::f32 maxExtent = rc::Max(extent.x, rc::Max(extent.y, extent.z));
            m_fit = (maxExtent > 0.0001f) ? (kTargetSize / maxExtent) : 1.0f;

            // All materials, indexed by SubMesh::materialIndex (multi-material).
            m_modelMats.Reserve(m_model->materials.Size());
            for (auto& mp : m_model->materials) { m_modelMats.PushBack(rc::RefPtr<mat::Material>(mp.Get())); }

            // Clips available for random per-instance selection (variety so the herd never lockstep).
            for (auto& clip : m_model->animations) { if (clip) { m_clips.PushBack(clip.Get()); } }
            return true;
        }

        // Spawn one instance of the cooked model at `position`: its own node hierarchy under a scaled
        // root (mesh nodes get MeshComponents referencing the SHARED cooked meshes/materials), plus its
        // own AnimationPlayer over the shared skeleton.
        void SpawnInstance(rc::Vec3 position)
        {
            auto* meshes = m_scene->GetSystem<rd::MeshComponentManager>();
            if (meshes == nullptr || !m_model) { return; }

            sc::EntityHandle modelRoot = m_scene->CreateEntity(u8"char");
            rc::Transform rootT;
            rootT.position = position;
            rootT.scale    = rc::Vec3{ m_fit, m_fit, m_fit };
            m_scene->SetLocalTransform(modelRoot, rootT);
            Instance inst;
            inst.root = modelRoot;

            rc::Array<sc::EntityHandle> entities;
            rc::Array<sc::EntityHandle> skinnedEntities;
            entities.Reserve(m_model->nodes.Size());
            for (const mi::ModelNode& node : m_model->nodes) {
                sc::EntityHandle e = m_scene->CreateEntity(node.name.AsView());
                m_scene->SetLocalTransform(e, node.localTransform);
                entities.PushBack(e);
            }
            for (rc::usize i = 0; i < m_model->nodes.Size(); ++i) {
                const mi::ModelNode& node = m_model->nodes[i];
                if (node.parentIndex >= 0 && static_cast<rc::usize>(node.parentIndex) < entities.Size()) {
                    m_scene->SetParent(entities[i], entities[static_cast<rc::usize>(node.parentIndex)]);
                } else {
                    m_scene->SetParent(entities[i], modelRoot);   // top-level node -> the scaled model root
                }
                if (node.meshIndex < 0 || static_cast<rc::usize>(node.meshIndex) >= m_model->meshes.Size()) { continue; }
                geo::StaticMesh* mesh = m_model->meshes[static_cast<rc::usize>(node.meshIndex)].Get();
                if (mesh == nullptr) { continue; }
                rd::MeshComponent& mc = meshes->Add(entities[i]);
                mc.mesh  = rc::RefPtr<geo::StaticMesh>(mesh);
                mc.color = rc::Color{ 1.0f, 1.0f, 1.0f, 1.0f };
                mc.submeshMaterials = m_modelMats;
                const rc::i32 matIdx = (static_cast<rc::usize>(node.meshIndex) < m_model->meshMaterial.Size())
                                           ? m_model->meshMaterial[static_cast<rc::usize>(node.meshIndex)] : -1;
                if (matIdx >= 0 && static_cast<rc::usize>(matIdx) < m_modelMats.Size()) {
                    mc.material = m_modelMats[static_cast<rc::usize>(matIdx)];
                }
                if (mesh->IsSkinned()) { skinnedEntities.PushBack(entities[i]); }
            }

            // Attach a SkeletalAnimationComponent on the root: the AnimationSubsystem ticks its player
            // each frame (PostUpdate) and feeds the skinning matrices to the skinned mesh entities. Random
            // clip + speed jitter + randomized start so the herd desyncs (à la Sedulous EngineAnimationSandbox).
            if (m_model->skeleton && !m_clips.IsEmpty() && skinnedEntities.Size() > 0) {
                if (auto* anims = m_scene->GetSystem<anim::SkeletalAnimationComponentManager>()) {
                    anim::AnimationClip* clip = m_clips[static_cast<rc::usize>(m_rng.NextInt(0, static_cast<rc::i32>(m_clips.Size()) - 1))];
                    anim::SkeletalAnimationComponent& a = anims->Add(modelRoot);
                    a.skeleton     = m_model->skeleton.Get();
                    a.clip         = clip;
                    a.meshEntities = static_cast<rc::Array<sc::EntityHandle>&&>(skinnedEntities);
                    a.speed        = 0.85f + m_rng.NextFloat() * 0.3f;
                    a.startTime    = (clip != nullptr && clip->duration > 0.0f) ? m_rng.NextFloat() * clip->duration : 0.0f;
                }
            }
            m_instances.PushBack(static_cast<Instance&&>(inst));
        }

        // Rebuild the whole grid to `count` characters: destroy the current instances, then spawn a fresh
        // square grid (side = ceil(sqrt(count))) centered on the origin, and re-frame the camera on it.
        void RebuildToCount(rc::u32 count)
        {
            for (Instance& inst : m_instances) { m_scene->DestroyEntity(inst.root); }   // recurses -> frees comps
            m_instances.Clear();   // frees the per-instance players

            const rc::u32 side = (count == 0) ? 1u : static_cast<rc::u32>(rc::Ceil(rc::Sqrt(static_cast<rc::f32>(count))));
            const rc::f32 half = (static_cast<rc::f32>(side) - 1.0f) * 0.5f;
            for (rc::u32 i = 0; i < count; ++i) {
                const rc::f32 px = (static_cast<rc::f32>(i % side) - half) * kCharacterSpacing;
                const rc::f32 pz = (static_cast<rc::f32>(i / side) - half) * kCharacterSpacing;
                SpawnInstance(rc::Vec3{ px, kFloorY, pz });
            }
            AutoFrame(side);
            m_frameTimeMs = 16.6f;   // reset the smoother so the rebuild hitch doesn't skew the reading
            rc::ConsoleWrite(rc::Format(u8"AnimStressTest: characters={}\n", m_instances.Size()));
        }

        // Position the fly camera so the whole side×side grid is in frame + grow the floor under it (called
        // on every batch change).
        void AutoFrame(rc::u32 side)
        {
            const rc::f32 extent = (static_cast<rc::f32>(side) - 1.0f) * kCharacterSpacing * 0.5f + kCharacterSize;
            // Floor: scale the base plane so it covers the whole grid + margin (uniform XZ; Y stays flat).
            const rc::f32 fscale = rc::Max(1.0f, (extent * 2.0f + 40.0f) / kFloorBaseSize);
            rc::Transform ft = m_scene->GetLocalTransform(m_floor);
            ft.scale = rc::Vec3{ fscale, 1.0f, fscale };
            m_scene->SetLocalTransform(m_floor, ft);
            const rc::Vec3 target{ 0.0f, kFloorY + kCharacterSize * 0.5f, 0.0f };   // grid center
            const rc::f32 dist = extent / rc::Tan(0.5236f) + kCharacterSize * 2.0f;  // fit 60° FOV horizontally + margin
            const rc::f32 camY = extent * 0.55f + kCharacterSize;
            m_fly.position = rc::Vec3{ target.x, target.y + camY, target.z + dist };
            m_fly.yaw      = 0.0f;
            m_fly.pitch    = -rc::Atan2(camY, dist);   // look down onto the grid center
            // Extend the far plane to cover the whole grid from this distance, so no characters get
            // frustum-far-culled (which would make the throughput measurement cheaper than it is).
            if (auto* cameras = m_scene->GetSystem<rd::CameraComponentManager>()) {
                if (rd::CameraComponent* cam = cameras->Get(m_camera)) {
                    cam->nearZ = 0.5f;
                    cam->farZ  = dist + extent * 2.0f + 100.0f;
                }
            }
        }

        void AddBatch()    { RebuildToCount(static_cast<rc::u32>(m_instances.Size()) + kBatchSize); }
        void RemoveBatch() {
            const rc::u32 n = static_cast<rc::u32>(m_instances.Size());
            RebuildToCount(n > kBatchSize ? n - kBatchSize : 0u);
        }

        // Single full-screen view via the default render path (reads the scene's primary camera).
        // Keep the camera's aspect synced to the backbuffer before delegating.
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
            rt::DefaultApplication::OnUpdate(host, deltaTime);   // keep the P-key profiling dump

            // ImGui HUD: open the frame + build the stats window (drawn in OnRenderWindow).
            if (auto* g = host.Ctx().GetSubsystem<gui::ImguiSubsystem>()) {
                g->NewFrame(host.Platform() != nullptr ? host.Platform()->Input() : nullptr, deltaTime);
                BuildHud(host.Ctx().GetSubsystem<rd::RenderSubsystem>());
            }

            // Fly camera (WASD/QE move, RMB/Tab look, Shift fast). Drives the scene camera entity; Esc exits.
            m_fly.Update(host, deltaTime);
            if (auto* input = host.Platform() != nullptr ? host.Platform()->Input() : nullptr) {
                if (rt::IKeyboard* kb = input->Keyboard()) {
                    if (kb->IsKeyPressed(rt::KeyCode::Space))     { AddBatch(); }
                    if (kb->IsKeyPressed(rt::KeyCode::Backspace)) { RemoveBatch(); }
                    if (kb->IsKeyPressed(rt::KeyCode::H))         { m_showHud = !m_showHud; }
                    if (kb->IsKeyPressed(rt::KeyCode::I)) {   // toggle prepass->forward instance-data sharing (A/B)
                        if (auto* render = host.Ctx().GetSubsystem<rd::RenderSubsystem>()) {
                            const bool on = !render->InstanceSharing();
                            render->SetInstanceSharing(on);
                            rc::ConsoleWrite(on ? u8"Instance sharing: ON\n" : u8"Instance sharing: OFF (forward re-fills)\n");
                        }
                    }
                    if (kb->IsKeyPressed(rt::KeyCode::Escape)) { host.RequestExit(0); return; }
                }
            }
            if (m_scene != nullptr) {
                rc::Transform camT = m_scene->GetLocalTransform(m_camera);
                camT.position = m_fly.position;
                camT.rotation = m_fly.Rotation();
                m_scene->SetLocalTransform(m_camera, camT);
            }

            if (m_scene == nullptr) { return; }

            // Animation is now driven by the engine's AnimationSubsystem (it ticks each entity's
            // SkeletalAnimationComponent in the scene's PostUpdate phase and feeds the bone matrices);
            // the sample no longer drives players by hand.

            // Smoothed FPS/frame-ms (vsync off -> real frame cost); shown in the ImGui HUD.
            m_frameTimeMs = m_frameTimeMs * 0.9f + (deltaTime * 1000.0f) * 0.1f;

            // TEMP headless auto-profile: hold a fixed count, warm up, then dump CPU + GPU profiler
            // reports (same as the P key) and exit — for capturing the baseline frame breakdown.
            if (kAutoProfile) {
                m_profileElapsed += deltaTime;
                if (m_profileElapsed >= 5.0f) {
                    const rc::f32 fps = (m_frameTimeMs > 0.001f) ? (1000.0f / m_frameTimeMs) : 0.0f;
                    rc::ConsoleWrite(rc::Format(u8"=== AnimStressTest PROFILE: chars={}  fps={}  frame={} ms ===\n",
                        m_instances.Size(), static_cast<rc::u32>(fps + 0.5f), m_frameTimeMs));
                    rc::ConsoleWrite(draconic::profiler::Profiler::Get().BuildReport().AsView());
                    if (auto* renderer = host.Ctx().GetSubsystem<rd::RenderSubsystem>()) {
                        rc::String gpu; renderer->BuildGpuProfileReport(gpu); rc::ConsoleWrite(gpu.AsView());
                    }
                    host.RequestExit(0); return;
                }
            }

            // TEMP headless auto-ramp: grow the count until FPS settles at/below 50, then report + exit.
            // Coarse (+25%) while well above 50, fine (+kBatchSize) near the knee, for a precise threshold.
            if (kAutoRamp) {
                m_rampSettle += deltaTime;
                if (m_rampSettle >= 1.3f) {
                    m_rampSettle = 0.0f;
                    const rc::f32 fps = (m_frameTimeMs > 0.001f) ? (1000.0f / m_frameTimeMs) : 0.0f;
                    const rc::u32 n = static_cast<rc::u32>(m_instances.Size());
                    if (fps <= 50.0f) {
                        rc::ConsoleWrite(rc::Format(u8"AnimStressTest: THRESHOLD chars={}  fps={}  frame={} ms\n",
                            n, static_cast<rc::u32>(fps + 0.5f), m_frameTimeMs));
                        host.RequestExit(0); return;
                    }
                    const rc::u32 next = (fps > 60.0f) ? rc::Max(n + 50u, n + n / 4u) : (n + kBatchSize);
                    RebuildToCount(next);
                }
            }

        }

        // ImGui HUD: character count + frame stats + exposure/bloom controls (H toggles it).
        void BuildHud(rd::RenderSubsystem* render)
        {
            if (!m_showHud) { return; }
            ImGui::Begin("Anim Stress Test");
            const float fps = m_frameTimeMs > 0.001f ? 1000.0f / m_frameTimeMs : 0.0f;
            ImGui::Text("%.0f fps   %.2f ms", static_cast<double>(fps), static_cast<double>(m_frameTimeMs));
            ImGui::Text("characters: %d", static_cast<int>(m_instances.Size()));
            if (render != nullptr) {
                ImGui::Separator();
                float exposure = render->Exposure();
                if (ImGui::SliderFloat("Exposure", &exposure, 0.05f, 4.0f)) { render->SetExposure(exposure); }
                bool bloomOn = render->BloomEnabled();
                if (ImGui::Checkbox("Bloom", &bloomOn)) { render->SetBloomEnabled(bloomOn); }
                bool inst = render->InstanceSharing();
                if (ImGui::Checkbox("Instance sharing (I)", &inst)) { render->SetInstanceSharing(inst); }
                ImGui::Separator();
                ImGui::TextUnformatted("Directional shadows");
                float shadowDist = render->ShadowDistance();
                if (ImGui::SliderFloat("Distance", &shadowDist, 50.0f, 1000.0f, "%.0f")) { render->SetShadowDistance(shadowDist); }
                float shadowFade = render->ShadowFarFade();
                if (ImGui::SliderFloat("Far fade", &shadowFade, 2.0f, 150.0f, "%.0f")) { render->SetShadowFarFade(shadowFade); }
                ImGui::TextDisabled("shadows fade out over the last %.0f units", static_cast<double>(shadowFade));
            }
            ImGui::Separator();
            if (ImGui::Button("+ batch (Space)")) { AddBatch(); }
            ImGui::SameLine();
            if (ImGui::Button("- batch (Backspace)")) { RemoveBatch(); }
            ImGui::TextUnformatted("H hide HUD   P profiler   Esc exit");
            ImGui::TextUnformatted("WASD/QE move   RMB look   Shift fast");
            ImGui::End();
        }

        void OnShutdown(rt::IApplicationHost& host) override
        {
            if (auto* gfx = host.Graphics(); gfx != nullptr && gfx->Raw() != nullptr) { gfx->Raw()->WaitIdle(); }
            rc::ConsoleWrite(u8"AnimStressTest: shutting down.\n");
        }

    private:
        sc::Scene*                  m_scene = nullptr;
        sc::EntityHandle            m_camera{};
        sc::EntityHandle            m_floor{};
        smp::FlyCamera              m_fly{ .position = rc::Vec3{ 0.0f, 10.0f, 26.0f }, .pitch = -0.25f };

        // Model-import pipeline state (must outlive the spawned entities — the resource manager owns
        // the cooked products' handles; the content DB + its filesystem mount back the manager).
        rc::UniquePtr<vfs::NativeFileSystem> m_contentFs;
        rc::UniquePtr<ct::ContentDatabase>   m_contentDb;
        rc::UniquePtr<res::ResourceManager>  m_resources;
        geo::StaticMeshFactory               m_meshFactory;
        geo::SkinnedMeshFactory              m_skinnedMeshFactory;
        mat::MaterialFactory                 m_materialFactory;
        anim::SkeletonFactory                m_skeletonFactory;
        anim::AnimationClipFactory           m_clipFactory;
        rc::UniquePtr<tex::TextureFactory>   m_textureFactory;   // needs the device
        mi::ModelFactory                     m_modelFactory;
        res::Proxy<mi::ModelResource>        m_model;       // the one cooked model, shared by every instance
        rc::Array<rc::RefPtr<mat::Material>> m_modelMats;   // its materials (indexed by submesh material index)
        rc::Array<anim::AnimationClip*>      m_clips;       // clips for random per-instance selection
        rc::f32                              m_fit = 1.0f;  // auto-fit scale

        // One spawned character: just its root entity (DestroyEntity recurses to free the hierarchy +
        // its SkeletalAnimationComponent). The component (engine-driven) owns the player + targets.
        struct Instance {
            sc::EntityHandle root{};
        };
        rc::Array<Instance> m_instances;
        rc::Random          m_rng{ 0x9e3779b97f4a7c15ull };
        rc::f32             m_frameTimeMs = 16.6f;
        bool                m_showHud     = true;   // HUD visibility (H)

        // Measurement aid (off by default): auto-ramp the character count until FPS <= 50, then
        // report + exit. Flip to true for a headless throughput baseline; normal use is interactive.
        static constexpr bool kAutoRamp = false;
        rc::f32             m_rampSettle = 0.0f;
        // Measurement aid (off by default): hold a fixed count, then dump the CPU+GPU profiler
        // breakdown and exit. Flip true to re-capture the baseline frame breakdown headless.
        static constexpr bool kAutoProfile = false;
        static constexpr rc::u32 kProfileCount = 1000;
        rc::f32             m_profileElapsed = 0.0f;
    };
}

int main(int, char**)
{
    auto platform = rt::CreatePlatform();
    rt::GraphicsDeviceDesc gpuDesc{};
    auto gpu = rt::CreateGraphicsDevice(gpuDesc);
    rt::GraphicsDevice* device = gpu.HasValue() ? gpu.Value().Get() : nullptr;

    AnimStressTestApp app;
    return rt::RunApplication(app, *platform, device);
}
