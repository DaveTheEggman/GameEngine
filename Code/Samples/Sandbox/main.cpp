// Sandbox — the running dev harness. It extends DefaultApplication (which registers
// the SceneSubsystem + RenderSubsystem and renders active scenes each frame), creates a
// scene with two spinning cube grids (instanced + distinct), and lets the engine draw it.
// As the renderer grows, this is where we exercise it.

#include "Core/Prelude.h"

import raptor.core;
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
                fmc.material = mat::CreatePBR(u8"lit", rc::Vec4{ 0.5f, 0.5f, 0.53f, 1.0f }, 0.0f, 0.65f);

                rc::RefPtr<geo::StaticMesh> cube = geo::Primitives::Cube(0.35f);
                BuildGrid(*meshes, cube, /*originX*/ -8.0f, /*instanced*/ true);
                BuildGrid(*meshes, cube, /*originX*/  8.0f, /*instanced*/ false);

                // A row of cubes resting EXACTLY on the floor (bottom face flush at y=-7) — a static
                // reference for judging shadow contact / peter-panning (the grids float in the air).
                constexpr rc::f32 kBoxSize = 2.5f, kFloorY = -7.0f;
                rc::RefPtr<geo::StaticMesh> box = geo::Primitives::Cube(kBoxSize);
                rc::RefPtr<mat::Material> boxMat = mat::CreatePBR(u8"lit", rc::Vec4{ 0.85f, 0.55f, 0.2f, 1.0f }, 0.0f, 0.5f);
                for (int k = 0; k < 4; ++k) {
                    sc::EntityHandle b = m_scene->CreateEntity(u8"floorBox");
                    m_scene->SetLocalPosition(b, rc::Vec3{ -7.5f + 5.0f * static_cast<rc::f32>(k), kFloorY + kBoxSize * 0.5f, 10.0f });
                    rd::MeshComponent& bmc = meshes->Add(b);
                    bmc.mesh = box; bmc.material = boxMat;
                }

                // Spheres resting ON the floor (bottom flush) — their contact shadow is mostly hidden
                // under the sphere, so you see only the "half" extending away from the sun (vs the
                // floating grids, whose full shadow ellipse is visible on the ground).
                constexpr rc::f32 kBallR = 1.25f;
                rc::RefPtr<geo::StaticMesh> ball = geo::Primitives::Sphere(kBallR, 24, 12);
                rc::RefPtr<mat::Material> ballMat = mat::CreatePBR(u8"lit", rc::Vec4{ 0.7f, 0.75f, 0.8f, 1.0f }, 0.1f, 0.35f);
                for (int k = 0; k < 4; ++k) {
                    sc::EntityHandle s = m_scene->CreateEntity(u8"floorBall");
                    m_scene->SetLocalPosition(s, rc::Vec3{ -7.5f + 5.0f * static_cast<rc::f32>(k), kFloorY + kBallR, 16.0f });
                    rd::MeshComponent& smc = meshes->Add(s);
                    smc.mesh = ball; smc.material = ballMat;
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
                kl.color = rc::Color{ 0.4f, 0.5f, 0.7f, 1.0f };
                kl.intensity = 0.5f;                        // key light: bright enough that its shadow reads
                kl.castsShadows = true;                     // directional CSM (5.2) + spot (5.3a) + point cube (5.3b)

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

                // A bright spot light overhead, aimed down at the floor boxes/spheres — the phase 5.3
                // atlas spot-shadow demo. Its cone casts sharp shadows of the resting boxes onto the
                // floor (distinct from the directional CSM), packed into the local-shadow atlas.
                sc::EntityHandle spot = m_scene->CreateEntity(u8"spotLight");
                m_scene->SetLocalPosition(spot, rc::Vec3{ 0.0f, 7.0f, 13.0f });   // between box row (z=10) and sphere row (z=16)
                rc::Transform st = m_scene->GetLocalTransform(spot);
                st.rotation = rc::Quat::FromAxisAngle(rc::Vec3{ 1.0f, 0.0f, 0.0f }, -1.5f);  // nearly straight down
                m_scene->SetLocalTransform(spot, st);
                rd::LightComponent& sl = lights->Add(spot);
                sl.type         = rd::LightType::Spot;
                sl.color        = rc::Color{ 1.0f, 0.92f, 0.78f, 1.0f };   // warm, to contrast the blue key
                sl.intensity    = 180.0f;                                  // inverse-square over ~14u to the floor
                sl.range        = 30.0f;
                sl.innerAngle   = 0.55f;
                sl.outerAngle   = 0.75f;                                   // wide cone: cover both the box + sphere rows
                sl.castsShadows = true;                                     // spot atlas shadow caster (5.3a)
                sl.shadowUpdate = rd::ShadowUpdateMode::Static;            // static scene -> cached atlas layer (5.4b)

                // A shadow-casting POINT light hovering among the floor boxes/spheres — the phase 5.3b
                // cube-shadow demo. Its 6 atlas faces cast shadows radially (onto the floor + box sides).
                sc::EntityHandle pt = m_scene->CreateEntity(u8"shadowPoint");
                m_scene->SetLocalPosition(pt, rc::Vec3{ 4.0f, -2.0f, 13.0f });
                rd::LightComponent& pls = lights->Add(pt);
                pls.type         = rd::LightType::Point;
                pls.color        = rc::Color{ 0.5f, 1.0f, 0.6f, 1.0f };     // green, distinct from the warm spot
                pls.intensity    = 40.0f;
                pls.range        = 16.0f;
                pls.castsShadows = true;                                     // point cube atlas caster (5.3b)
            }

            LoadImportedModel(host);   // cook + spawn a glTF model through the resource pipeline

            rc::ConsoleWrite(u8"Sandbox: split-screen — same scene from two cameras, 18 clustered "
                             u8"point lights. Close to exit.\n");
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

            // A few imported models side by side (runtime cook seam; an editor would cook offline + Bind).
            SpawnModel(u8"Duck", rc::Format(u8"{}/Duck/glTF/Duck.gltf", modelDir).AsView(), rc::Vec3{ -5.0f, -4.0f, 6.0f });
            SpawnModel(u8"Fox",  rc::Format(u8"{}/Fox/glTF/Fox.gltf",  modelDir).AsView(), rc::Vec3{  5.0f, -7.0f, 6.0f });
            SpawnModel(u8"Char", rc::Format(u8"{}/QuaterniusCharacter/glTF/Character.gltf", modelDir).AsView(), rc::Vec3{ 0.0f, -7.0f, 12.0f });
        }

        // Cook + bind + spawn one model, placed at `position` and auto-fit to a target size. Each model
        // spawns its node hierarchy (local TRS + parent links) under a scaled model-root entity; mesh
        // nodes get a MeshComponent referencing the cooked StaticMesh + material.
        void SpawnModel(rc::StringView prefix, rc::StringView path, rc::Vec3 position)
        {
            auto* meshes = m_scene->GetSystem<rd::MeshComponentManager>();
            if (meshes == nullptr || m_contentDb.Get() == nullptr) { return; }

            rc::Guid modelGuid;
            const mdl::ModelLoadResult r = mi::LoadAndCook(path, *m_contentDb, prefix, modelGuid);
            if (r != mdl::ModelLoadResult::Ok) {
                rc::ConsoleWrite(rc::Format(u8"Sandbox: model import failed ({}) for {}\n",
                                            static_cast<rc::u32>(r), prefix));
                return;
            }
            res::Proxy<mi::ModelResource> model = m_resources->Bind<mi::ModelResource>(modelGuid);
            if (!model) { rc::ConsoleWrite(u8"Sandbox: model bind failed\n"); return; }

            // Auto-fit: the model-root scales the model's largest extent to a target size (models come in
            // wildly different unit scales — the Duck is ~100 units, the Fox ~150).
            constexpr rc::f32 kTargetSize = 6.0f;
            const rc::Vec3 extent = model->boundsMax - model->boundsMin;
            const rc::f32 maxExtent = rc::Max(extent.x, rc::Max(extent.y, extent.z));
            const rc::f32 fit = (maxExtent > 0.0001f) ? (kTargetSize / maxExtent) : 1.0f;
            sc::EntityHandle modelRoot = m_scene->CreateEntity(prefix);
            rc::Transform rootT;
            rootT.position = position;
            rootT.scale    = rc::Vec3{ fit, fit, fit };
            m_scene->SetLocalTransform(modelRoot, rootT);

            // All the model's materials, indexed by SubMesh::materialIndex (= model material index) for
            // per-submesh (multi-material) rendering. The resource manager keeps them alive via m_models.
            rc::Array<rc::RefPtr<mat::Material>> modelMats;
            modelMats.Reserve(model->materials.Size());
            for (auto& mp : model->materials) { modelMats.PushBack(rc::RefPtr<mat::Material>(mp.Get())); }

            rc::Array<sc::EntityHandle> entities;
            rc::Array<sc::EntityHandle> skinnedEntities;
            entities.Reserve(model->nodes.Size());
            for (const mi::ModelNode& node : model->nodes) {
                sc::EntityHandle e = m_scene->CreateEntity(node.name.AsView());
                m_scene->SetLocalTransform(e, node.localTransform);
                entities.PushBack(e);
            }
            for (rc::usize i = 0; i < model->nodes.Size(); ++i) {
                const mi::ModelNode& node = model->nodes[i];
                if (node.parentIndex >= 0 && static_cast<rc::usize>(node.parentIndex) < entities.Size()) {
                    m_scene->SetParent(entities[i], entities[static_cast<rc::usize>(node.parentIndex)]);
                } else {
                    m_scene->SetParent(entities[i], modelRoot);   // top-level node -> the scaled model root
                }
                if (node.meshIndex < 0 || static_cast<rc::usize>(node.meshIndex) >= model->meshes.Size()) { continue; }
                geo::StaticMesh* mesh = model->meshes[static_cast<rc::usize>(node.meshIndex)].Get();
                if (mesh == nullptr) { continue; }
                rd::MeshComponent& mc = meshes->Add(entities[i]);
                mc.mesh  = rc::RefPtr<geo::StaticMesh>(mesh);   // hold a ref (manager owns the handle)
                mc.color = rc::Color{ 1.0f, 1.0f, 1.0f, 1.0f };

                // Per-submesh materials (the mesh's submeshes index modelMats); + a single-material
                // fallback (first submesh's material) for the whole-mesh path.
                mc.submeshMaterials = modelMats;
                const rc::i32 matIdx = (static_cast<rc::usize>(node.meshIndex) < model->meshMaterial.Size())
                                           ? model->meshMaterial[static_cast<rc::usize>(node.meshIndex)] : -1;
                if (matIdx >= 0 && static_cast<rc::usize>(matIdx) < model->materials.Size()) {
                    if (mat::Material* material = model->materials[static_cast<rc::usize>(matIdx)].Get()) {
                        mc.material = rc::RefPtr<mat::Material>(material);
                    }
                }
                if (mesh->IsSkinned()) { skinnedEntities.PushBack(entities[i]); }
            }
            m_models.PushBack(model);   // keep the model (and its resources) alive

            // If the model is skinned + animated, drive it: one AnimationPlayer over its skeleton plays
            // the first clip, and each frame feeds its skinning matrices to the skinned mesh components.
            if (model->skeleton && model->animations.Size() > 0 && model->animations[0] &&
                skinnedEntities.Size() > 0) {
                AnimatedModel am;
                am.player = rc::MakeUnique<anim::AnimationPlayer>(rc::DefaultAllocator(), *model->skeleton.Get());
                am.player->Play(model->animations[0].Get());
                am.meshEntities = static_cast<rc::Array<sc::EntityHandle>&&>(skinnedEntities);
                m_animated.PushBack(static_cast<AnimatedModel&&>(am));
            }
        }

        // Split-screen rendered into an OFFSCREEN texture, then blitted to the backbuffer — the
        // editor-shaped path (a view renders to a sampleable/copyable target, not straight to the
        // swapchain). Exercises the full multi-view path + the configurable target final state.
        void OnRenderWindow(rt::IApplicationHost& host, rt::FrameContext& frame) override
        {
            auto* render = host.Ctx().GetSubsystem<rd::RenderSubsystem>();
            auto* gfx    = host.Graphics();
            if (m_scene == nullptr || render == nullptr || !render->IsReady() ||
                gfx == nullptr || gfx->Raw() == nullptr || frame.encoder == nullptr ||
                frame.backbuffer == nullptr || frame.window == nullptr) {
                return;
            }
            rhi::Device& device = *gfx->Raw();
            auto fmt = frame.window->Swap()->Format();
            // One offscreen per frame-in-flight: frame N+1 must not render into the target frame N's
            // blit still reads. (A single shared offscreen across in-flight frames races on resize.)
            const rc::u32 slot = frame.frameIndex < kOffscreenSlots ? frame.frameIndex : 0u;
            if (!EnsureOffscreen(device, fmt, frame.width, frame.height, slot)) { return; }
            rhi::Texture*     offTex  = m_offscreenTex[slot];
            rhi::TextureView* offView = m_offscreenView[slot];

            const rc::u32 halfW  = frame.width / 2;
            const rc::f32 aspect = static_cast<rc::f32>(halfW) / static_cast<rc::f32>(frame.height);
            auto makeCam = [&](rc::Vec3 eye, rc::Vec3 target) {
                rd::ViewCamera vc;
                vc.view       = rc::Mat4::LookAtRH(eye, target, rc::Vec3{ 0.0f, 1.0f, 0.0f });
                vc.projection = rc::Mat4::PerspectiveFovRH(1.0472f, aspect, 0.1f, 1000.0f);
                vc.position   = eye;
                vc.farZ       = 1000.0f;
                return vc;
            };
            // The fly camera drives whichever view is selected (V toggles); the other stays put.
            const rd::ViewCamera flyCam = makeCam(m_fly.position, m_fly.position + m_fly.Forward());
            rd::CameraOverride camL;
            camL.camera = (m_controlledView == 0) ? flyCam : makeCam(rc::Vec3{ -6.0f, 14.0f, 30.0f }, rc::Vec3{ 0.0f, -2.0f, 0.0f });
            camL.clearColor = rc::Color{ 0.02f, 0.02f, 0.03f, 1.0f };
            rd::CameraOverride camR;
            camR.camera = (m_controlledView == 1) ? flyCam : makeCam(rc::Vec3{  6.0f, 14.0f, 30.0f }, rc::Vec3{ 0.0f, -2.0f, 0.0f });
            camR.clearColor = rc::Color{ 0.02f, 0.02f, 0.03f, 1.0f };

            // Render both views into this slot's offscreen texture; the graph leaves it in CopySrc.
            rd::TargetState ts{ offTex, m_offscreenState[slot], rhi::ResourceState::CopySrc };
            render->BeginRendering(*frame.encoder, frame.frameIndex);
            render->RenderScene(*m_scene, offView, fmt, frame.width, frame.height,
                                rd::ViewportRect{ 0, 0, halfW, frame.height }, &camL, ts);
            render->RenderScene(*m_scene, offView, fmt, frame.width, frame.height,
                                rd::ViewportRect{ static_cast<rc::i32>(halfW), 0, frame.width - halfW, frame.height }, &camR, ts);
            render->EndRendering();
            m_offscreenState[slot] = rhi::ResourceState::CopySrc;

            // Blit the offscreen result onto the backbuffer (the host then presents it).
            frame.encoder->TransitionTexture(frame.backbuffer, rhi::ResourceState::RenderTarget, rhi::ResourceState::CopyDst);
            frame.encoder->Blit(offTex, frame.backbuffer);
            frame.encoder->TransitionTexture(frame.backbuffer, rhi::ResourceState::CopyDst, rhi::ResourceState::RenderTarget);
        }

        // Create (or resize) one frame-slot's offscreen color target. Each slot tracks its own size,
        // so a resize lazily recreates each slot as it next renders.
        bool EnsureOffscreen(rhi::Device& device, rhi::TextureFormat fmt, rc::u32 w, rc::u32 h, rc::u32 slot)
        {
            if (m_offscreenTex[slot] != nullptr && m_offscreenW[slot] == w && m_offscreenH[slot] == h) { return true; }
            device.WaitIdle();
            if (m_offscreenView[slot] != nullptr) { device.DestroyTextureView(m_offscreenView[slot]); m_offscreenView[slot] = nullptr; }
            if (m_offscreenTex[slot]  != nullptr) { device.DestroyTexture(m_offscreenTex[slot]);       m_offscreenTex[slot]  = nullptr; }

            rhi::TextureDesc td{};
            td.format = fmt; td.width = w; td.height = h;
            td.usage  = rhi::TextureUsage::RenderTarget | rhi::TextureUsage::Sampled | rhi::TextureUsage::CopySrc;
            td.label  = u8"sandbox.offscreen";
            if (!device.CreateTexture(td, m_offscreenTex[slot]).IsOk()) { m_offscreenTex[slot] = nullptr; return false; }
            rhi::TextureViewDesc vd{}; vd.format = fmt;
            if (!device.CreateTextureView(m_offscreenTex[slot], vd, m_offscreenView[slot]).IsOk()) { m_offscreenView[slot] = nullptr; return false; }
            m_offscreenW[slot] = w; m_offscreenH[slot] = h; m_offscreenState[slot] = rhi::ResourceState::Undefined;
            return true;
        }

        void OnUpdate(rt::IApplicationHost& host, rc::f32 deltaTime) override
        {
            rt::DefaultApplication::OnUpdate(host, deltaTime);   // keep the P-key profiling dump

            // Fly camera (WASD/QE move, RMB/Tab look, Shift fast). V toggles which split-screen view
            // it drives; Esc exits.
            m_fly.Update(host, deltaTime);
            if (auto* input = host.Platform() != nullptr ? host.Platform()->Input() : nullptr) {
                if (rt::IKeyboard* kb = input->Keyboard()) {
                    if (kb->IsKeyPressed(rt::KeyCode::V)) { m_controlledView = 1u - m_controlledView; }
                    if (kb->IsKeyPressed(rt::KeyCode::Escape)) { host.RequestExit(0); return; }
                }
            }

            if (m_scene == nullptr) { return; }

            // Drive skinned models: advance each animation player, then hand its per-bone skinning
            // matrices to the model's skinned mesh components (borrowed for the frame; extraction copies
            // the pointer, the renderer uploads them to the bone pool).
            if (auto* meshes = m_scene->GetSystem<rd::MeshComponentManager>()) {
                for (AnimatedModel& am : m_animated) {
                    if (am.player.Get() == nullptr) { continue; }
                    am.player->Update(deltaTime);
                    const rc::Span<const rc::Mat4> mats = am.player->GetSkinningMatrices();
                    for (sc::EntityHandle e : am.meshEntities) {
                        if (rd::MeshComponent* mc = meshes->Get(e)) {
                            mc->boneMatrices = mats.Data();
                            mc->boneCount    = static_cast<rc::u32>(mats.Size());
                        }
                    }
                }
            }

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

        void OnShutdown(rt::IApplicationHost& host) override
        {
            if (auto* gfx = host.Graphics(); gfx != nullptr && gfx->Raw() != nullptr) {
                rhi::Device& device = *gfx->Raw();
                device.WaitIdle();   // GPU must finish before freeing the offscreen targets
                for (rc::u32 i = 0; i < kOffscreenSlots; ++i) {
                    if (m_offscreenView[i] != nullptr) { device.DestroyTextureView(m_offscreenView[i]); m_offscreenView[i] = nullptr; }
                    if (m_offscreenTex[i]  != nullptr) { device.DestroyTexture(m_offscreenTex[i]);       m_offscreenTex[i]  = nullptr; }
                }
            }
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
                shared = mat::CreatePBR(u8"lit", rc::Vec4{ 1.0f, 1.0f, 1.0f, 1.0f }, 0.0f, 0.4f);
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
                        mc.material = mat::CreatePBR(u8"lit", baseColor, metal, rough);
                        mc.color = rc::Color{ 1.0f, 1.0f, 1.0f, 1.0f };
                    }
                    m_cubes.PushBack(e);
                }
            }
        }

    private:
        sc::Scene*                  m_scene = nullptr;
        sc::EntityHandle            m_camera{};
        // Offscreen render target, double-buffered per frame-in-flight (each slot tracks its own size).
        static constexpr rc::u32    kOffscreenSlots = 3;
        rhi::Texture*               m_offscreenTex[kOffscreenSlots]   = {};
        rhi::TextureView*           m_offscreenView[kOffscreenSlots]  = {};
        rhi::ResourceState          m_offscreenState[kOffscreenSlots] = { rhi::ResourceState::Undefined, rhi::ResourceState::Undefined, rhi::ResourceState::Undefined };
        rc::u32                     m_offscreenW[kOffscreenSlots] = {}, m_offscreenH[kOffscreenSlots] = {};
        rc::Array<sc::EntityHandle> m_pointLights;
        rc::Array<rc::Vec3>         m_lightBases;
        rc::Array<sc::EntityHandle> m_cubes;
        rc::f32                     m_angle = 0.0f;
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
        rc::Array<res::Proxy<mi::ModelResource>> m_models;   // keep cooked models + their resources alive

        // A spawned skinned+animated model: a player over its skeleton + the skinned mesh entities it
        // feeds. Driven each frame in OnUpdate (Update -> GetSkinningMatrices -> MeshComponent).
        struct AnimatedModel {
            rc::UniquePtr<anim::AnimationPlayer> player;
            rc::Array<sc::EntityHandle>          meshEntities;
        };
        rc::Array<AnimatedModel>             m_animated;
        rc::u32                     m_controlledView = 0;   // which split-screen view the fly cam drives (V toggles)
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
