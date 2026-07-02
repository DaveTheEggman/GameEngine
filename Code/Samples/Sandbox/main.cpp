// Sandbox — the running dev harness. It extends DefaultApplication (which registers
// the SceneSubsystem + RenderSubsystem and renders active scenes each frame), creates a
// scene with two spinning cube grids (instanced + distinct), and lets the engine draw it.
// As the renderer grows, this is where we exercise it.

#include "Core/Prelude.h"
#include "imgui.h"   // Dear ImGui (debug UI) — used directly; the engine integration is draconic.imgui

import draconic.core;
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
import draconic.animation.subsystem;    // SkeletalAnimation/AnimationGraph components (engine-driven skinning)
import draconic.imgui;                   // ImguiSubsystem (debug UI)
import draconic.image;                   // Image (HDR equirect pixels)
import draconic.image.io;                // LoadImage
import draconic.render;                  // ViewCamera / ViewportRect (split-screen overrides)
import draconic.geometry;
import draconic.geometry.resource;       // StaticMeshFactory + StaticMesh product
import draconic.materials;
import draconic.materials.resource;       // MaterialFactory (cooked materials)
import draconic.texture.resource;         // TextureFactory (cooked textures)
import draconic.texture.editor;           // TextureImporter::LoadCubemap
import draconic.animation.resource;       // Skeleton/AnimationClip factories
import draconic.vfs;                      // NativeFileSystem mount for the content DB
import draconic.content;                  // ContentDatabase (cooked-resource output)
import draconic.resource;                 // ResourceManager + Proxy
import draconic.model;                    // ModelLoadResult
import draconic.modelimporter;            // LoadAndCook + ImportedModel manifest
import draconic.animation;                // AnimationPlayer (drives GPU skinning)

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
namespace geo = draconic::geometry;
namespace mat = draconic::materials;
namespace tex = draconic::texture;
namespace vfs = draconic::vfs;
namespace ct  = draconic::content;
namespace res = draconic::resource;
namespace mdl  = draconic::model;
namespace mi   = draconic::modelimporter;
namespace anim = draconic::animation;
namespace gui = draconic::imgui;

namespace
{
    class SandboxApp final : public rt::DefaultApplication
    {
    public:
        // Register the standard subsystems (DefaultApplication) + the ImGui debug UI on top.
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

            // Per-scene environment: drives IBL (split-sum ambient from a procedural sky) + the flat
            // fallback. The procedural sky's gradient + sun feed the SH9 diffuse + prefiltered specular.
            if (auto* env = m_scene->GetSystem<rd::EnvironmentSystem>()) {
                rd::EnvironmentSettings& e = env->Environment();
                e.ambientColor     = rc::Color{ 0.12f, 0.16f, 0.28f, 1.0f };   // flat fallback (IBL off)
                e.ambientIntensity = 0.35f;
                e.skyMode      = rd::SkyMode::Procedural;
                e.skyIntensity = 0.7f;   // dimmer sky -> less washed-out IBL ambient
                e.skyHorizon   = rc::Color{ 0.62f, 0.70f, 0.85f, 1.0f };
                e.skyZenith    = rc::Color{ 0.18f, 0.34f, 0.68f, 1.0f };
                e.skyGround    = rc::Color{ 0.28f, 0.26f, 0.24f, 1.0f };
                e.sunIntensity = 1.0f;
            }

            // Load an HDR equirectangular environment so F5 can cycle to it (mode starts Procedural).
            if (auto* render = host.Ctx().GetSubsystem<rd::RenderSubsystem>()) {
                // Default exposure below 1.0 — the procedural sky + IBL ambient are bright, so the AgX
                // tonemap washes out at 1.0. Tune live via the Environment window's Exposure slider.
                render->SetExposure(0.5f);
                rc::String hdrPath = rc::Format(u8"{}/BlueSky.hdr",
                    rc::StringView(reinterpret_cast<const rc::utf8char*>(DRACONIC_SANDBOX_ENV_DIR)));
                draconic::image::Image img;
                if (draconic::image::io::LoadImage(hdrPath.AsView(), img).IsOk() && img.Format() == draconic::image::PixelFormat::RGBA32F) {
                    const rc::Span<const rc::u8> px = img.PixelData();
                    const rc::Span<const rc::f32> rgba{ reinterpret_cast<const rc::f32*>(px.Data()), px.Size() / sizeof(rc::f32) };
                    render->SetSkyEquirect(img.Width(), img.Height(), rgba);
                    rc::ConsoleWrite(rc::Format(u8"Sandbox: loaded HDR sky {}x{} (F5 / Sky Mode combo to use it)\n",
                                                img.Width(), img.Height()).AsView());
                } else {
                    rc::ConsoleWrite(rc::Format(u8"Sandbox: FAILED to load HDR sky from {}\n", hdrPath.AsView()).AsView());
                }

                // Cubemap source: point at ONE face; the importer detects the other 5 (px/nx/...) and
                // loads + combines them. (Explicit 6-path LoadCubemap also works.)
                rc::String oneFace = rc::Format(u8"{}/cube_sky/px.png",
                    rc::StringView(reinterpret_cast<const rc::utf8char*>(DRACONIC_SANDBOX_ENV_DIR)));
                rc::Array<rc::String> facePaths;
                if (tex::TextureImporter::DetectCubemapFaces(oneFace.AsView(), facePaths).IsOk() && facePaths.Size() == 6) {
                    rc::StringView faceViews[6];
                    for (int i = 0; i < 6; ++i) { faceViews[i] = facePaths[static_cast<rc::usize>(i)].AsView(); }
                    rc::Array<rc::u8> cube; rc::u32 cubeFace = 0;
                    if (tex::TextureImporter::LoadCubemap(rc::Span<const rc::StringView>{ faceViews, 6 }, cube, cubeFace).IsOk()) {
                        render->SetSkyCubemap(cubeFace, rc::Span<const rc::u8>{ cube.Data(), cube.Size() });
                        rc::ConsoleWrite(rc::Format(u8"Sandbox: loaded cubemap sky {}x{} x6\n", cubeFace, cubeFace).AsView());
                    }
                }
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

                // Transparent (alpha-blended) spheres hovering in front of the opaque row — exercises the
                // transparent path: routed to the Transparent category (blend mode), sorted back-to-front,
                // blended with depth-test/no-write. Base-color alpha (<1) makes them see-through.
                rc::RefPtr<mat::Material> glassMat = mat::CreatePBR(u8"lit", rc::Vec4{ 0.35f, 0.6f, 0.95f, 0.4f }, 0.0f, 0.12f);
                glassMat->pipeline.blendMode = mat::BlendMode::AlphaBlend;
                glassMat->pipeline.depthMode = mat::DepthMode::ReadOnly;   // test against opaque depth, don't write
                for (int k = 0; k < 3; ++k) {
                    sc::EntityHandle g = m_scene->CreateEntity(u8"glassBall");
                    m_scene->SetLocalPosition(g, rc::Vec3{ -5.0f + 5.0f * static_cast<rc::f32>(k), kFloorY + 3.5f, 20.0f });
                    rd::MeshComponent& gmc = meshes->Add(g);
                    gmc.mesh = ball; gmc.material = glassMat;
                }

                // Masked (alpha-tested) spheres: a checkerboard alpha-cutout texture drives the discard,
                // so they render with real holes (opaque where solid, gone where the texture alpha is 0).
                if (rhi::Device* dev = (host.Graphics() != nullptr) ? host.Graphics()->Raw() : nullptr) {
                    if (rhi::TextureView* cutout = CreateCutoutTexture(*dev)) {
                        rc::RefPtr<mat::Material> maskMat = mat::CreatePBR(u8"lit", rc::Vec4{ 0.95f, 0.8f, 0.3f, 1.0f }, 0.0f, 0.45f);
                        maskMat->pipeline.blendMode = mat::BlendMode::Masked;
                        maskMat->SetDefaultTexture(u8"AlbedoMap", cutout);   // alpha holes -> discard
                        for (int k = 0; k < 3; ++k) {
                            sc::EntityHandle m = m_scene->CreateEntity(u8"maskedBall");
                            m_scene->SetLocalPosition(m, rc::Vec3{ -5.0f + 5.0f * static_cast<rc::f32>(k), kFloorY + 3.5f, 24.0f });
                            rd::MeshComponent& mmc = meshes->Add(m);
                            mmc.mesh = ball; mmc.material = maskMat;
                        }
                    }
                }
            }

            // Lights: a dim directional key (down-forward) + a bright point light that orbits the
            // grid in OnUpdate, so the per-light forward shade is visible (moving highlight).
            if (auto* lights = m_scene->GetSystem<rd::LightComponentManager>()) {
                sc::EntityHandle key = m_scene->CreateEntity(u8"keyLight");
                m_keyLight = key;
                ApplyKeyLightDir();   // pitch/yaw -> entity rotation (steeper downward tilt by default)
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
                        pl.intensity = 14.0f;
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
                sl.intensity    = 120.0f;                                  // inverse-square over ~14u to the floor
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
                pls.intensity    = 28.0f;
                pls.range        = 16.0f;
                pls.castsShadows = true;                                     // point cube atlas caster (5.3b)
            }

            LoadImportedModel(host);   // cook + spawn a glTF model through the resource pipeline

            rc::ConsoleWrite(u8"Sandbox: split-screen — same scene from two cameras, 18 clustered "
                             u8"point lights. G cycles the Character's animation-graph state. Close to exit.\n");
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

            // A few imported models side by side (runtime cook seam; an editor would cook offline + Bind).
            SpawnModel(u8"Duck", rc::Format(u8"{}/Duck/glTF/Duck.gltf", modelDir).AsView(), rc::Vec3{ -5.0f, -4.0f, 6.0f });
            SpawnModel(u8"Fox",  rc::Format(u8"{}/Fox/glTF/Fox.gltf",  modelDir).AsView(), rc::Vec3{  5.0f, -7.0f, 6.0f });
            // The Character is driven by an AnimationGraph (a state machine over its clips) rather than a
            // single clip — press G to fire the graph's "Next" trigger and cross-fade to the next state.
            SpawnModel(u8"Char", rc::Format(u8"{}/QuaterniusCharacter/glTF/Character.gltf", modelDir).AsView(), rc::Vec3{ 0.0f, -7.0f, 12.0f }, /*useGraph=*/true);
        }

        // Cook + bind + spawn one model, placed at `position` and auto-fit to a target size. Each model
        // spawns its node hierarchy (local TRS + parent links) under a scaled model-root entity; mesh
        // nodes get a MeshComponent referencing the cooked StaticMesh + material.
        void SpawnModel(rc::StringView prefix, rc::StringView path, rc::Vec3 position, bool useGraph = false)
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

            // If the model is skinned + animated, hand it to the animation subsystem: attach a component
            // to the model root + list its skinned mesh nodes as the feed targets. The subsystem ticks
            // the player each frame and writes the skinning matrices into those MeshComponents — no
            // per-frame driving in app code. A graph-driven model gets an AnimationGraphComponent (a
            // state machine over its clips); everything else gets a single-clip SkeletalAnimationComponent.
            if (model->skeleton && model->animations.Size() > 0 && model->animations[0] &&
                skinnedEntities.Size() > 0) {
                if (useGraph) {
                    if (auto* graphMgr = m_scene->GetSystem<anim::AnimationGraphComponentManager>()) {
                        rc::RefPtr<anim::AnimationGraph> graph = BuildClipCyclerGraph(*model);
                        anim::AnimationGraphComponent& gc = graphMgr->Add(modelRoot);
                        gc.skeleton     = model->skeleton.Get();
                        gc.graph        = graph.Get();
                        gc.meshEntities = static_cast<rc::Array<sc::EntityHandle>&&>(skinnedEntities);
                        m_graphs.PushBack(static_cast<rc::RefPtr<anim::AnimationGraph>&&>(graph));   // keep alive
                        m_graphChar = modelRoot;                                                     // G drives this one
                    }
                } else if (auto* skelMgr = m_scene->GetSystem<anim::SkeletalAnimationComponentManager>()) {
                    anim::SkeletalAnimationComponent& sa = skelMgr->Add(modelRoot);
                    sa.skeleton     = model->skeleton.Get();
                    sa.clip         = model->animations[0].Get();
                    sa.meshEntities = static_cast<rc::Array<sc::EntityHandle>&&>(skinnedEntities);
                }
            }
        }

        // Build a simple state-machine graph over a model's clips: one Clip state per animation, plus a
        // "Next" trigger that cross-fades each state to the following one (wrapping). Demonstrates the
        // AnimationGraph machinery (states, transitions, parameters, cross-fades) without authored data.
        rc::RefPtr<anim::AnimationGraph> BuildClipCyclerGraph(mi::ModelResource& model)
        {
            rc::RefPtr<anim::AnimationGraph> graph = rc::MakeRef<anim::AnimationGraph>(rc::DefaultAllocator());
            const rc::i32 nextParam = graph->AddParameter(u8"Next", anim::AnimationParameterType::Trigger);

            auto layer = rc::MakeUnique<anim::AnimationLayer>(rc::DefaultAllocator(), rc::StringView(u8"Base"));
            const rc::i32 clipCount = static_cast<rc::i32>(model.animations.Size());
            for (rc::i32 i = 0; i < clipCount; ++i) {
                anim::AnimationClip* clip = model.animations[static_cast<rc::usize>(i)].Get();
                auto state = rc::MakeUnique<anim::AnimationGraphState>(
                    rc::DefaultAllocator(), clip != nullptr ? clip->Name().AsView() : rc::StringView(u8"State"),
                    rc::MakeUnique<anim::ClipStateNode>(rc::DefaultAllocator(), clip));
                layer->AddState(static_cast<rc::UniquePtr<anim::AnimationGraphState>&&>(state));
            }
            // state[i] --Next--> state[(i+1) % N], cross-fading over 0.25s.
            for (rc::i32 i = 0; i < clipCount; ++i) {
                auto t = rc::MakeUnique<anim::AnimationGraphTransition>(rc::DefaultAllocator());
                t->sourceStateIndex = i;
                t->destStateIndex   = (i + 1) % clipCount;
                t->duration         = 0.25f;
                t->AddBoolCondition(nextParam, true);
                layer->AddTransition(static_cast<rc::UniquePtr<anim::AnimationGraphTransition>&&>(t));
            }
            graph->AddLayer(static_cast<rc::UniquePtr<anim::AnimationLayer>&&>(layer));
            return graph;
        }

        // Fire the Character graph's "Next" trigger, advancing its state machine to the next clip. The
        // player is owned + created lazily by the AnimationGraphComponentManager, so reach it through the
        // component (it exists once the subsystem has ticked at least once).
        void FireGraphNext()
        {
            if (m_scene == nullptr || !m_graphChar.IsAssigned()) { return; }
            auto* graphMgr = m_scene->GetSystem<anim::AnimationGraphComponentManager>();
            if (graphMgr == nullptr) { return; }
            if (anim::AnimationGraphComponent* gc = graphMgr->Get(m_graphChar)) {
                if (gc->player.Get() != nullptr) { gc->player->SetTrigger(rc::StringView(u8"Next")); }
            }
        }

        // Aim the directional key light from pitch (downward tilt) + yaw (compass) angles. The light
        // shines along the entity's forward (-Z), so rotation = yaw(Y) * pitch(X); the sky sun tracks it.
        void ApplyKeyLightDir()
        {
            if (m_scene == nullptr || !m_keyLight.IsAssigned()) { return; }
            rc::Transform t = m_scene->GetLocalTransform(m_keyLight);
            t.rotation = rc::Quat::FromAxisAngle(rc::Vec3{ 0.0f, 1.0f, 0.0f }, m_keyYaw)
                       * rc::Quat::FromAxisAngle(rc::Vec3{ 1.0f, 0.0f, 0.0f }, m_keyPitch);
            m_scene->SetLocalTransform(m_keyLight, t);
        }

        // Cycle the sky source (Procedural -> Analytic -> HDR Equirect -> Cubemap -> ...). Changing
        // skyMode re-runs the IBL precompute from the new source next frame.
        void CycleSkyMode()
        {
            if (m_scene == nullptr) { return; }
            if (auto* env = m_scene->GetSystem<rd::EnvironmentSystem>()) {
                rd::EnvironmentSettings& e = env->Environment();
                e.skyMode = (e.skyMode == rd::SkyMode::Procedural)  ? rd::SkyMode::Analytic :
                            (e.skyMode == rd::SkyMode::Analytic)    ? rd::SkyMode::HDREquirect :
                            (e.skyMode == rd::SkyMode::HDREquirect) ? rd::SkyMode::Cubemap :
                                                                      rd::SkyMode::Procedural;
            }
        }

        // Live debug UI (ImGui): scene environment tweakables wired straight to EnvironmentSettings —
        // editing these re-runs the IBL precompute next frame, so the ambient updates live.
        void BuildDebugUI(rd::RenderSubsystem* render)
        {
            if (m_scene == nullptr) { return; }
            ImGui::Begin("Environment");
            if (render != nullptr) {
                float exposure = render->Exposure();
                if (ImGui::SliderFloat("Exposure", &exposure, 0.05f, 8.0f)) { render->SetExposure(exposure); }
                {
                    // Ambient occlusion mode (Off/GTAO/SSAO, mutually exclusive) + shared knobs.
                    const char* aoItems[] = { "Off", "GTAO", "SSAO" };
                    int aoMode = static_cast<int>(render->GetAoMode());
                    if (ImGui::Combo("AO Mode", &aoMode, aoItems, 3)) { render->SetAoMode(static_cast<rd::AoMode>(aoMode)); }
                    if (render->GetAoMode() != rd::AoMode::Off) {
                        float s = render->AoStrength();
                        if (ImGui::SliderFloat("AO Strength", &s, 0.0f, 1.0f)) { render->SetAoStrength(s); }
                        float rad = render->AoRadius();
                        if (ImGui::SliderFloat("AO Radius", &rad, 0.1f, 3.0f)) { render->SetAoRadius(rad); }
                        float inten = render->AoIntensity();
                        if (ImGui::SliderFloat("AO Power", &inten, 0.5f, 4.0f)) { render->SetAoIntensity(inten); }
                    }
                    // Debug view (forces a generator on, shows the chosen buffer straight to screen): a good
                    // view-space normal ramps smoothly with orientation; View Z ramps with distance; AO
                    // should darken only in creases.
                    const char* dbgItems[] = { "Off", "AO", "Normal.x", "Normal.y", "Normal.z", "View Z", "Raw Depth" };
                    int dbg = render->AoDebug();
                    if (ImGui::Combo("AO Debug", &dbg, dbgItems, 7)) { render->SetAoDebug(dbg); }
                }
                bool taaOn = render->TaaEnabled();
                if (ImGui::Checkbox("TAA", &taaOn)) { render->SetTaaEnabled(taaOn); }
                if (!taaOn) {   // FXAA is the TAA-off fallback (never stacked with TAA)
                    bool fxaaOn = render->FxaaEnabled();
                    if (ImGui::Checkbox("FXAA", &fxaaOn)) { render->SetFxaaEnabled(fxaaOn); }
                    if (fxaaOn) {
                        float sq = render->FxaaSubpixel();
                        if (ImGui::SliderFloat("FXAA Subpixel", &sq, 0.0f, 1.0f)) { render->SetFxaaSubpixel(sq); }
                    }
                }
                if (taaOn) {
                    float b = render->TaaBlend();
                    if (ImGui::SliderFloat("TAA History", &b, 0.80f, 0.995f)) { render->SetTaaBlend(b); }
                    float g = render->TaaGamma();
                    if (ImGui::SliderFloat("TAA Variance", &g, 0.5f, 2.5f)) { render->SetTaaGamma(g); }
                    float m = render->TaaMotionScale();
                    if (ImGui::SliderFloat("TAA Motion", &m, 0.0f, 128.0f)) { render->SetTaaMotionScale(m); }
                }
                bool bloomOn = render->BloomEnabled();
                if (ImGui::Checkbox("Bloom", &bloomOn)) { render->SetBloomEnabled(bloomOn); }
                if (bloomOn) {
                    float bloom = render->BloomIntensity();
                    if (ImGui::SliderFloat("Bloom Intensity", &bloom, 0.0f, 0.5f)) { render->SetBloomIntensity(bloom); }
                    float bloomThresh = render->BloomThreshold();
                    if (ImGui::SliderFloat("Bloom Threshold", &bloomThresh, 0.0f, 4.0f)) { render->SetBloomThreshold(bloomThresh); }
                }
            }
            if (auto* env = m_scene->GetSystem<rd::EnvironmentSystem>()) {
                rd::EnvironmentSettings& e = env->Environment();
                // Sky source selector (also F5 to cycle). Picking a mode re-runs the IBL precompute.
                const char* modes[] = { "Procedural", "Analytic (Preetham)", "HDR Equirect", "Cubemap" };
                int modeIdx = (e.skyMode == rd::SkyMode::Analytic)    ? 1 :
                              (e.skyMode == rd::SkyMode::HDREquirect)  ? 2 :
                              (e.skyMode == rd::SkyMode::Cubemap)      ? 3 : 0;
                if (ImGui::Combo("Sky Mode", &modeIdx, modes, 4)) {
                    e.skyMode = (modeIdx == 1) ? rd::SkyMode::Analytic :
                                (modeIdx == 2) ? rd::SkyMode::HDREquirect :
                                (modeIdx == 3) ? rd::SkyMode::Cubemap : rd::SkyMode::Procedural;
                }
                ImGui::SliderFloat("Sky Intensity", &e.skyIntensity, 0.0f, 4.0f);

                const bool procedural = (e.skyMode == rd::SkyMode::Procedural);
                const bool analytic   = (e.skyMode == rd::SkyMode::Analytic);
                const bool untextured = procedural || analytic;   // has an analytic sun disc
                // Only show controls relevant to the selected mode.
                if (untextured) {
                    ImGui::SliderFloat("Sun Intensity", &e.sunIntensity, 0.0f, 8.0f);
                    ImGui::SliderFloat("Sun Size (deg)", &e.sunAngularSize, 0.1f, 10.0f);
                }
                if (analytic) {
                    ImGui::SliderFloat("Turbidity", &e.turbidity, 1.7f, 10.0f);
                }
                if (procedural) {
                    float h[3]  = { e.skyHorizon.r, e.skyHorizon.g, e.skyHorizon.b };
                    if (ImGui::ColorEdit3("Horizon", h)) { e.skyHorizon = rc::Color{ h[0], h[1], h[2], 1.0f }; }
                    float z[3]  = { e.skyZenith.r, e.skyZenith.g, e.skyZenith.b };
                    if (ImGui::ColorEdit3("Zenith", z)) { e.skyZenith = rc::Color{ z[0], z[1], z[2], 1.0f }; }
                    float gr[3] = { e.skyGround.r, e.skyGround.g, e.skyGround.b };
                    if (ImGui::ColorEdit3("Ground", gr)) { e.skyGround = rc::Color{ gr[0], gr[1], gr[2], 1.0f }; }
                }
            }
            // Directional (key) light — the actual scene illumination + sky sun direction. Distinct from
            // "Sun Intensity" above (that's the sky's sun disc brightness, not the light that shades surfaces).
            if (auto* lights = m_scene->GetSystem<rd::LightComponentManager>()) {
                if (rd::LightComponent* kl = m_keyLight.IsAssigned() ? lights->Get(m_keyLight) : nullptr) {
                    ImGui::SeparatorText("Directional Light");
                    ImGui::SliderFloat("Light Intensity", &kl->intensity, 0.0f, 8.0f);
                    float lc[3] = { kl->color.r, kl->color.g, kl->color.b };
                    if (ImGui::ColorEdit3("Light Color", lc)) { kl->color = rc::Color{ lc[0], lc[1], lc[2], 1.0f }; }
                    // Aim the light live (pitch = downward tilt, yaw = compass). SliderAngle shows degrees.
                    bool dirChanged = false;
                    dirChanged |= ImGui::SliderAngle("Pitch", &m_keyPitch, -89.0f, 0.0f);
                    dirChanged |= ImGui::SliderAngle("Yaw",   &m_keyYaw,  -180.0f, 180.0f);
                    if (dirChanged) { ApplyKeyLightDir(); }
                }
            }
            ImGui::Checkbox("Show ImGui demo", &m_showImguiDemo);
            ImGui::End();
            if (m_showImguiDemo) { ImGui::ShowDemoWindow(&m_showImguiDemo); }
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

            // Debug UI on top of the scene (backbuffer is RenderTarget again; ImGui loads + draws over it).
            if (auto* g = host.Ctx().GetSubsystem<gui::ImguiSubsystem>()) { g->Render(frame); }
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

            // ImGui debug UI: open the frame (feed input + size) then build the tweakables. The draw
            // data is rendered over the scene in OnRenderWindow.
            if (auto* g = host.Ctx().GetSubsystem<gui::ImguiSubsystem>()) {
                g->NewFrame(host.Platform() != nullptr ? host.Platform()->Input() : nullptr, deltaTime);
                BuildDebugUI(host.Ctx().GetSubsystem<rd::RenderSubsystem>());
            }

            // Fly camera (WASD/QE move, RMB/Tab look, Shift fast). V toggles which split-screen view
            // it drives; Esc exits.
            m_fly.Update(host, deltaTime);
            if (auto* input = host.Platform() != nullptr ? host.Platform()->Input() : nullptr) {
                if (rt::IKeyboard* kb = input->Keyboard()) {
                    if (kb->IsKeyPressed(rt::KeyCode::V)) { m_controlledView = 1u - m_controlledView; }
                    if (kb->IsKeyPressed(rt::KeyCode::Escape)) { host.RequestExit(0); return; }
                    // G fires the Character graph's "Next" trigger -> cross-fade to its next clip state.
                    if (kb->IsKeyPressed(rt::KeyCode::G)) { FireGraphNext(); }
                    // F5 cycles the sky source (procedural <-> HDR equirectangular).
                    if (kb->IsKeyPressed(rt::KeyCode::F5)) { CycleSkyMode(); }
                }
            }

            if (m_scene == nullptr) { return; }

            // Skinned models are advanced by the animation subsystem (SkeletalAnimation/AnimationGraph
            // components ticked on the scene's PostUpdate phase) — no per-frame driving here anymore.

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

            // Debug-draw demo: per-scene gizmos (wire boxes/spheres on the resting props, origin axes,
            // a ground grid, an arrow from the spot, 3D + screen text). Both split-screen views render
            // these, each projected through its OWN camera — and they're keyed to this scene, so a second
            // scene's gizmos would never bleed in.
            if (auto* render = host.Ctx().GetSubsystem<rd::RenderSubsystem>()) {
                auto& dbg = render->Debug(*m_scene);
                for (int k = 0; k < 4; ++k) {
                    const rc::Vec3 boxC{ -7.5f + 5.0f * static_cast<rc::f32>(k), -5.75f, 10.0f };
                    dbg.DrawWireBoxCenter(boxC, rc::Vec3{ 1.3f, 1.3f, 1.3f }, rc::Color{ 1.0f, 1.0f, 0.0f, 1.0f });
                    const rc::Vec3 ballC{ -7.5f + 5.0f * static_cast<rc::f32>(k), -5.75f, 16.0f };
                    dbg.DrawWireSphere(rc::BoundingSphere{ ballC, 1.4f }, rc::Color{ 0.2f, 0.9f, 1.0f, 1.0f });
                }
                dbg.DrawAxis(rc::Mat4::Identity(), 3.0f, /*overlay*/ true);
                dbg.DrawGrid(rc::Vec3{ 0.0f, -6.99f, 0.0f }, 60.0f, 30, rc::Color{ 0.25f, 0.25f, 0.30f, 1.0f });
                dbg.DrawArrow(rc::Vec3{ 0.0f, 7.0f, 13.0f }, rc::Vec3{ 0.0f, -6.5f, 13.0f }, rc::Color{ 1.0f, 0.5f, 0.0f, 1.0f });
                dbg.DrawText3D(rc::Vec3{ 0.0f, 0.5f, 0.0f }, rc::StringView(u8"origin"), rc::Color{ 1.0f, 1.0f, 1.0f, 1.0f });
                render->Debug().DrawScreenText(12.0f, 12.0f, rc::StringView(u8"Draconic Debug Draw"), rc::Color{ 0.6f, 1.0f, 0.6f, 1.0f }, 2.0f);
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
                if (m_cutoutView != nullptr) { device.DestroyTextureView(m_cutoutView); m_cutoutView = nullptr; }
                if (m_cutoutTex  != nullptr) { device.DestroyTexture(m_cutoutTex);       m_cutoutTex  = nullptr; }
            }
            rc::ConsoleWrite(u8"Sandbox: shutting down.\n");
        }

        // Build a small procedural alpha-cutout texture (checkerboard: opaque cells + fully-transparent
        // holes) and upload it synchronously via an RHI transfer batch. Feeds the masked-material demo,
        // whose alpha-test discard cuts the holes. Stores the texture/view for shutdown cleanup.
        rhi::TextureView* CreateCutoutTexture(rhi::Device& device)
        {
            constexpr rc::u32 N = 64, cell = 8;
            rc::Array<rc::u8> px; px.Resize(static_cast<rc::usize>(N) * N * 4);
            for (rc::u32 y = 0; y < N; ++y) {
                for (rc::u32 x = 0; x < N; ++x) {
                    const bool solid = ((((x / cell) + (y / cell)) & 1u) == 0u);
                    rc::u8* p = &px[(static_cast<rc::usize>(y) * N + x) * 4];
                    p[0] = 255; p[1] = 255; p[2] = 255; p[3] = solid ? 255 : 0;
                }
            }
            rhi::TextureDesc td{};
            td.dimension = rhi::TextureDimension::Texture2D; td.format = rhi::TextureFormat::RGBA8UnormSrgb;
            td.width = N; td.height = N; td.depth = 1; td.arrayLayerCount = 1; td.mipLevelCount = 1;
            td.usage = rhi::TextureUsage::Sampled | rhi::TextureUsage::CopyDst; td.label = u8"masked.cutout";
            if (!device.CreateTexture(td, m_cutoutTex).IsOk()) { m_cutoutTex = nullptr; return nullptr; }
            rhi::TextureViewDesc vd{}; vd.format = rhi::TextureFormat::RGBA8UnormSrgb;
            vd.dimension = rhi::TextureViewDimension::Texture2D; vd.mipLevelCount = 1; vd.arrayLayerCount = 1;
            if (!device.CreateTextureView(m_cutoutTex, vd, m_cutoutView).IsOk()) {
                device.DestroyTexture(m_cutoutTex); m_cutoutTex = nullptr; m_cutoutView = nullptr; return nullptr;
            }
            if (rhi::Queue* q = device.GetQueue(rhi::QueueType::Graphics, 0)) {
                rhi::TransferBatch* batch = nullptr;
                if (q->CreateTransferBatch(batch).IsOk() && batch != nullptr) {
                    rhi::TextureDataLayout layout{}; layout.bytesPerRow = N * 4; layout.rowsPerImage = N;
                    batch->WriteTexture(m_cutoutTex, rc::Span<const rc::u8>(px.Data(), px.Size()), layout, rhi::Extent3D{ N, N, 1 });
                    (void)batch->Submit();
                    q->DestroyTransferBatch(batch);
                }
            }
            return m_cutoutView;
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
        rhi::Texture*               m_cutoutTex  = nullptr;   // masked-demo alpha-cutout texture
        rhi::TextureView*           m_cutoutView = nullptr;
        sc::EntityHandle            m_keyLight;   // directional key light (intensity/color tweakable in the debug UI)
        rc::f32                     m_keyPitch = -1.05f;   // downward tilt (~-60 deg); Environment window slider
        rc::f32                     m_keyYaw   =  0.35f;   // compass heading
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

        // Animation graphs owned by the demo (the AnimationGraphComponents borrow them); the entity whose
        // graph the G key advances (the Character). Skinned models are otherwise driven by the subsystem.
        rc::Array<rc::RefPtr<anim::AnimationGraph>> m_graphs;
        sc::EntityHandle                            m_graphChar{};
        rc::u32                     m_controlledView = 0;   // which split-screen view the fly cam drives (V toggles)
        bool                        m_showImguiDemo  = false;   // debug-UI toggle
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
