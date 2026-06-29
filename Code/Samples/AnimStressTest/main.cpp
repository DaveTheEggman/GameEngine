// Sandbox — the running dev harness. It extends DefaultApplication (which registers
// the SceneSubsystem + RenderSubsystem and renders active scenes each frame), creates a
// scene with two spinning cube grids (instanced + distinct), and lets the engine draw it.
// As the renderer grows, this is where we exercise it.

#include "Core/Prelude.h"
#include "Profiler/Profiler.h"   // RAPTOR_PROFILE_SCOPE (isolate animation-drive cost)

import raptor.core;
import raptor.profiler;
import raptor.rhi;                     // offscreen render target (Texture / ResourceState / Blit)
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
import raptor.geometry.resource;       // StaticMeshFactory + StaticMesh product
import raptor.materials;
import raptor.materials.resource;       // MaterialFactory (cooked materials)
import raptor.texture.resource;         // TextureFactory (cooked textures)
import raptor.animation.resource;       // Skeleton/AnimationClip factories
import raptor.vfs;                      // NativeFileSystem mount for the content DB
import raptor.content;                  // ContentDatabase (cooked-resource output)
import raptor.resource;                 // ResourceManager + Proxy
import raptor.model;                    // ModelLoadResult
import raptor.modelimporter;            // LoadAndCook + ImportedModel manifest
import raptor.animation;                // AnimationPlayer (drives GPU skinning)

#include "../Common/FlyCamera.h"   // shared free-fly camera (uses the imported runtime/core types)

#ifndef RAPTOR_SANDBOX_MODEL_DIR
#define RAPTOR_SANDBOX_MODEL_DIR ""
#endif
#ifndef RAPTOR_SANDBOX_OUTPUT_DIR
#define RAPTOR_SANDBOX_OUTPUT_DIR ""
#endif

namespace rc = raptor::core;
namespace smp = raptor::samples;
namespace rhi = raptor::rhi;
namespace rt = raptor::runtime;
namespace sc = raptor::scene;
namespace rd = raptor::render;
namespace geo = raptor::geometry;
namespace mat = raptor::materials;
namespace tex = raptor::texture;
namespace vfs = raptor::vfs;
namespace ct  = raptor::content;
namespace res = raptor::resource;
namespace mdl  = raptor::model;
namespace mi   = raptor::modelimporter;
namespace anim = raptor::animation;

namespace
{
    // How many characters each +/- press adds or removes (and the initial spawn).
    static constexpr rc::u32 kBatchSize        = 25;
    static constexpr rc::f32 kCharacterSpacing = 8.0f;   // grid spacing (world units)
    static constexpr rc::f32 kCharacterSize    = 6.0f;   // auto-fit target height (matches CookModel)
    static constexpr rc::f32 kFloorY           = -7.0f;

    class AnimStressTestApp final : public rt::DefaultApplication
    {
    public:
        // Run uncapped (vsync off) so the frame time reflects real CPU+GPU skinning work, not the
        // display refresh — same as RenderStressTest. The image tears; fine for a benchmark.
        rt::ApplicationSettings Settings() const override
        {
            rt::ApplicationSettings s;
            s.presentMode = rhi::PresentMode::Immediate;
            return s;
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
                sc::EntityHandle floor = m_scene->CreateEntity(u8"floor");
                m_scene->SetLocalPosition(floor, rc::Vec3{ 0.0f, -7.0f, 0.0f });
                rd::MeshComponent& fmc = meshes->Add(floor);
                fmc.mesh = geo::Primitives::Plane(120.0f, 120.0f);
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

            rc::ConsoleWrite(u8"AnimStressTest: [Space] add batch  [Backspace] remove batch  [P] profiler  [Esc] exit\n");
        }

        // The model-import seam: open the cooked-resource output DB, register the geometry factory,
        // load+cook a glTF file through the importer, then spawn its node hierarchy as entities whose
        // MeshComponents reference the cooked StaticMesh resources. This is the clean runtime cook seam
        // the design calls for — an editor would cook offline and the runtime would only Bind, but the
        // wiring (factory -> Bind -> render) is identical.
        void LoadImportedModel(rt::IApplicationHost& host)
        {
            const rc::StringView outputDir(reinterpret_cast<const rc::utf8char*>(RAPTOR_SANDBOX_OUTPUT_DIR));
            const rc::StringView modelDir(reinterpret_cast<const rc::utf8char*>(RAPTOR_SANDBOX_MODEL_DIR));
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

            // Per-instance AnimationPlayer over the shared skeleton: random clip + speed jitter +
            // randomized start time so the herd desyncs (faithful to Sedulous's EngineAnimationSandbox).
            if (m_model->skeleton && !m_clips.IsEmpty() && skinnedEntities.Size() > 0) {
                inst.player = rc::MakeUnique<anim::AnimationPlayer>(rc::DefaultAllocator(), *m_model->skeleton.Get());
                anim::AnimationClip* clip = m_clips[static_cast<rc::usize>(m_rng.NextInt(0, static_cast<rc::i32>(m_clips.Size()) - 1))];
                inst.player->Play(clip);
                inst.player->speed = 0.85f + m_rng.NextFloat() * 0.3f;
                if (clip != nullptr && clip->duration > 0.0f) { inst.player->SetCurrentTime(m_rng.NextFloat() * clip->duration); }
                inst.meshEntities = static_cast<rc::Array<sc::EntityHandle>&&>(skinnedEntities);
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

        // Position the fly camera so the whole side×side grid is in frame (called on every batch change).
        void AutoFrame(rc::u32 side)
        {
            const rc::f32 extent = (static_cast<rc::f32>(side) - 1.0f) * kCharacterSpacing * 0.5f + kCharacterSize;
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
        }

        void OnUpdate(rt::IApplicationHost& host, rc::f32 deltaTime) override
        {
            rt::DefaultApplication::OnUpdate(host, deltaTime);   // keep the P-key profiling dump

            // Fly camera (WASD/QE move, RMB/Tab look, Shift fast). Drives the scene camera entity; Esc exits.
            m_fly.Update(host, deltaTime);
            if (auto* input = host.Platform() != nullptr ? host.Platform()->Input() : nullptr) {
                if (rt::IKeyboard* kb = input->Keyboard()) {
                    if (kb->IsKeyPressed(rt::KeyCode::Space))     { AddBatch(); }
                    if (kb->IsKeyPressed(rt::KeyCode::Backspace)) { RemoveBatch(); }
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

            // Drive every instance: advance its player, then hand its per-bone skinning matrices to its
            // skinned mesh components (borrowed for the frame; extraction copies the pointer).
            RAPTOR_PROFILE_SCOPE("Anim.Drive");
            if (auto* meshes = m_scene->GetSystem<rd::MeshComponentManager>()) {
                for (Instance& inst : m_instances) {
                    if (inst.player.Get() == nullptr) { continue; }
                    inst.player->Update(deltaTime);
                    const rc::Span<const rc::Mat4> mats     = inst.player->GetSkinningMatrices();
                    const rc::Span<const rc::Mat4> prevMats = inst.player->GetPrevSkinningMatrices();
                    for (sc::EntityHandle e : inst.meshEntities) {
                        if (rd::MeshComponent* mc = meshes->Get(e)) {
                            mc->boneMatrices     = mats.Data();
                            mc->prevBoneMatrices = prevMats.Data();
                            mc->boneCount        = static_cast<rc::u32>(mats.Size());
                        }
                    }
                }
            }

            // Smoothed FPS/frame-ms readout once a second (vsync off -> real frame cost).
            m_frameTimeMs = m_frameTimeMs * 0.9f + (deltaTime * 1000.0f) * 0.1f;
            m_reportTimer += deltaTime;
            if (m_reportTimer >= 1.0f) {
                m_reportTimer = 0.0f;
                const rc::f32 fps = (m_frameTimeMs > 0.001f) ? (1000.0f / m_frameTimeMs) : 0.0f;
                rc::ConsoleWrite(rc::Format(u8"AnimStressTest: chars={}  fps={}  frame={} ms\n",
                    m_instances.Size(), static_cast<rc::u32>(fps + 0.5f), m_frameTimeMs));
            }

            // TEMP headless auto-profile: hold a fixed count, warm up, then dump CPU + GPU profiler
            // reports (same as the P key) and exit — for capturing the baseline frame breakdown.
            if (kAutoProfile) {
                m_profileElapsed += deltaTime;
                if (m_profileElapsed >= 5.0f) {
                    const rc::f32 fps = (m_frameTimeMs > 0.001f) ? (1000.0f / m_frameTimeMs) : 0.0f;
                    rc::ConsoleWrite(rc::Format(u8"=== AnimStressTest PROFILE: chars={}  fps={}  frame={} ms ===\n",
                        m_instances.Size(), static_cast<rc::u32>(fps + 0.5f), m_frameTimeMs));
                    rc::ConsoleWrite(raptor::profiler::Profiler::Get().BuildReport().AsView());
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

        void OnShutdown(rt::IApplicationHost& host) override
        {
            if (auto* gfx = host.Graphics(); gfx != nullptr && gfx->Raw() != nullptr) { gfx->Raw()->WaitIdle(); }
            rc::ConsoleWrite(u8"AnimStressTest: shutting down.\n");
        }

    private:
        sc::Scene*                  m_scene = nullptr;
        sc::EntityHandle            m_camera{};
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

        // One spawned character: its root entity (DestroyEntity recurses), the skinned mesh entities it
        // feeds, and its own AnimationPlayer. Driven each frame in OnUpdate.
        struct Instance {
            sc::EntityHandle                     root{};
            rc::Array<sc::EntityHandle>          meshEntities;
            rc::UniquePtr<anim::AnimationPlayer> player;
        };
        rc::Array<Instance> m_instances;
        rc::Random          m_rng{ 0x9e3779b97f4a7c15ull };
        rc::f32             m_frameTimeMs = 16.6f;
        rc::f32             m_reportTimer = 0.0f;

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
