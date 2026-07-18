// RaptorPlayer - the generic game runner (MVP-to-Export milestone, docs/design/roadmap.md).
//
// Runs a project with ZERO native game code: engine subsystems + the project's content +
// the default scene, simulating - and, when the manifest names one, the project's GAME SCRIPT
// (the scripted IApplication counterpart: a Wren class `Game` with launch/update(dt)/exit,
// orchestrating above scenes). With scripting, player + scripts + cooked content IS the game;
// projects that outgrow scripts graduate to a native IApplication at the same seam.
//
// Usage: RaptorPlayer <projectDir> [--scene <source-db-path>] [--exit-after <seconds>]
//
// Two modes, detected by layout:
//   PROJECT dir (Project.xml): scenes load from the authored source DB (their cooked form IS
//     the authored form - scenes are builder-less by design), products resolve from Cooked/ -
//     the editor's own runtime path. The dev loop.
//   DIST dir (Content.pak + player.xml, staged by RaptorExport): ONE binary DB inside the pak
//     holds products AND scenes; the game script rides in the pak as a raw entry. Zero editor
//     code links into this binary - the shipping shape.

#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <filesystem>

#include "Core/Prelude.h"
#include "Core/Log/Log.h"
#include "Core/Reflection/Reflect.h"

import draconic.core;
import draconic.vfs;
import draconic.content;
import draconic.resource;
import draconic.shell;
import draconic.shell.desktop;
import draconic.graphics;
import draconic.graphics.gpu;
import draconic.runtime;
import draconic.runtime.client;
import draconic.runtime.desktop;
import draconic.runtime.defaultapp;
import draconic.scene;
import draconic.scene.subsystem;
import draconic.scene.resource;
import draconic.render;
import draconic.render.subsystem;
import draconic.animation;
import draconic.animation.resource;
import draconic.animation.subsystem;
import draconic.particles;
import draconic.particles.resource;
import draconic.particles.subsystem;
import draconic.geometry;
import draconic.geometry.resource;
import draconic.materials;
import draconic.materials.resource;
import draconic.texture;
import draconic.texture.resource;
import draconic.image.resource;
import draconic.modelimporter;
import draconic.script;
import draconic.script.wren;
import draconic.input;
import draconic.physics;
import draconic.physics.resource;
import draconic.physics.subsystem;
import draconic.input.resource;
import draconic.input.subsystem;
import draconic.xml.serialization;
import draconic.project;       // manifest + layout (runtime-side, editor-free)
import draconic.vfs.pak;       // dist mode: one Content.pak holds products + scenes + scripts

using namespace draconic::core;
namespace rt = draconic::runtime;
namespace shell = draconic::shell;
namespace graphics = draconic::graphics;
namespace dscene = draconic::scene;
namespace res = draconic::resource;

namespace
{
    struct PlayerOptions
    {
        String projectDir;
        String sceneOverride;   // source-DB path; empty = the manifest's defaultScene
        f32 exitAfterSeconds = 0.0f;
    };

    class PlayerApplication final : public rt::DefaultApplication
    {
    public:
        explicit PlayerApplication(PlayerOptions options) : m_options(Move(options)) {}

        void Configure(rt::IApplicationHost& host) override
        {
            rt::DefaultApplication::Configure(host);
            host.Ctx().AddSubsystem<draconic::particles::ParticleSubsystem>();
            m_physics = host.Ctx().AddSubsystem<draconic::physics::PhysicsSubsystem>();
            // FIRST in tick order matters not (input polls devices, scenes read the runtime);
            // shell devices wire in OnStartup once the shell exists.
            m_input = host.Ctx().AddSubsystem<draconic::input::InputSubsystem>(
                host.Shell() != nullptr ? host.Shell()->Input() : nullptr);

            // Product/runtime types: factories construct cooked products BY TYPE NAME.
            draconic::modelimporter::RegisterModelImporterTypes();
            draconic::image::RegisterImageResource();
            draconic::particles::RegisterParticleEffectResource();
            draconic::input::RegisterInputMapResource();
            draconic::physics::RegisterPhysicsResource();
            GlobalTypeRegistry().Register(dscene::SceneDocument::StaticType());
            RegisterSerializable<dscene::SceneDocument>();
        }

        void OnStartup(rt::IApplicationHost& host) override
        {
            namespace proj = draconic::project;
            m_root = MakeUnique<draconic::vfs::NativeFileSystem>(DefaultAllocator(), m_options.projectDir.AsView());

            // Dist layout wins when present (a staged dist can sit inside a project tree).
            if (m_root->Exists(proj::kDistContentPak))
            {
                m_pak = MakeUnique<draconic::vfs::PakFileSystem>(DefaultAllocator(),
                    PathJoin(m_options.projectDir.AsView(), proj::kDistContentPak).AsView());
                if (!m_pak->IsValid()
                    || !proj::LoadProjectSettings(*m_root, m_settings, proj::kDistManifestFile).IsOk())
                {
                    DRACONIC_LOG_ERROR(u8"Player", u8"dist at '{}' is unreadable", m_options.projectDir);
                    host.RequestExit(1);
                    return;
                }
                m_contentDb = MakeUnique<draconic::content::ContentDatabase>(DefaultAllocator(),
                    *m_pak, BinarySerializerFactory(), proj::kCookedAssetExtension);
                m_sceneDb = m_contentDb.Get();   // scenes live IN the pak, binary like products
                DRACONIC_LOG_INFO(u8"Player", u8"dist mode ({} pak entries)", m_pak->EntryCount());
            }
            else if (m_root->Exists(proj::kProjectManifestFile))
            {
                if (!proj::LoadProjectSettings(*m_root, m_settings).IsOk())
                {
                    DRACONIC_LOG_ERROR(u8"Player", u8"project manifest at '{}' is unreadable", m_options.projectDir);
                    host.RequestExit(1);
                    return;
                }
                m_contentMount = MakeUnique<draconic::vfs::NativeFileSystem>(DefaultAllocator(),
                    PathJoin(m_options.projectDir.AsView(), proj::kProjectContentDir).AsView());
                m_cookedMount = MakeUnique<draconic::vfs::NativeFileSystem>(DefaultAllocator(),
                    PathJoin(m_options.projectDir.AsView(), proj::kProjectCookedDir).AsView());
                m_sourceDb = MakeUnique<draconic::content::ContentDatabase>(DefaultAllocator(),
                    *m_contentMount, draconic::xml::XmlSerializerFactory(), proj::kSourceAssetExtension);
                m_contentDb = MakeUnique<draconic::content::ContentDatabase>(DefaultAllocator(),
                    *m_cookedMount, BinarySerializerFactory(), proj::kCookedAssetExtension);
                m_sceneDb = m_sourceDb.Get();   // authored scenes; products from the cooked DB
            }
            else
            {
                DRACONIC_LOG_ERROR(u8"Player", u8"'{}' is neither a project (Project.xml) nor a dist (Content.pak)",
                                   m_options.projectDir);
                host.RequestExit(1);
                return;
            }

            m_resources = MakeUnique<res::ResourceManager>(DefaultAllocator(), *m_contentDb);
            m_resources->AddFactory(&m_meshFactory);
            m_resources->AddFactory(&m_skinnedFactory);
            m_resources->AddFactory(&m_materialFactory);
            m_resources->AddFactory(&m_skeletonFactory);
            m_resources->AddFactory(&m_clipFactory);
            m_resources->AddFactory(&m_graphFactory);
            m_resources->AddFactory(&m_effectFactory);
            m_resources->AddFactory(&m_inputMapFactory);
            m_resources->AddFactory(&m_collisionShapeFactory);
            m_resources->AddFactory(&m_physicalMaterialFactory);
            if (graphics::GraphicsDevice* gfx = host.Graphics(); gfx != nullptr && gfx->Raw() != nullptr)
            {
                m_textureFactory = MakeUnique<draconic::texture::TextureFactory>(DefaultAllocator(), *gfx->Raw());
                m_resources->AddFactory(m_textureFactory.Get());
            }
        }

        void OnLaunch(rt::IApplicationHost& host) override
        {
            if (m_sceneDb == nullptr) { return; }
            auto* scenes = host.Ctx().GetSubsystem<dscene::SceneSubsystem>();
            if (scenes == nullptr) { return; }

            // Resolution order: --scene path override, the manifest's guid (authoritative,
            // rename-proof), then the path mirror (v2 manifests).
            draconic::content::Instance* instance = nullptr;
            if (!m_options.sceneOverride.IsEmpty())
            {
                instance = m_sceneDb->GetInstance(m_options.sceneOverride.AsView());
            }
            else
            {
                if (!m_settings.defaultSceneId.IsNil())
                {
                    instance = m_sceneDb->GetInstance(m_settings.defaultSceneId);
                }
                if (instance == nullptr && !m_settings.defaultScene.IsEmpty())
                {
                    instance = m_sceneDb->GetInstance(m_settings.defaultScene.AsView());
                }
            }
            if (instance == nullptr)
            {
                DRACONIC_LOG_ERROR(u8"Player", u8"no startup scene (manifest defaultScene/--scene unresolved)");
                host.RequestExit(1);
                return;
            }
            const String scenePath = instance->Path();

            m_scene = scenes->CreateScene(instance->Name());
            if (m_scene == nullptr || !dscene::LoadScene(*instance, *m_scene).IsOk())
            {
                DRACONIC_LOG_ERROR(u8"Player", u8"scene '{}' failed to load", scenePath);
                host.RequestExit(1);
                return;
            }
            dscene::ResolveSceneResources(*m_scene, *m_resources);
            // Prefab instances arrive as ref+deltas: respawn them from the same DB the
            // scene came from (pak mode: the staged payloads; project mode: the sources).
            if (m_scene->PendingPrefabInstanceCount() > 0)
            {
                draconic::content::ContentDatabase* sceneDb = m_sceneDb;
                dscene::ResolveScenePrefabs(*m_scene,
                    Function<UniquePtr<IStream>(const Guid&)>{
                        [sceneDb](const Guid& prefabId) -> UniquePtr<IStream> {
                            draconic::content::Instance* prefab =
                                (sceneDb != nullptr) ? sceneDb->GetInstance(prefabId) : nullptr;
                            return (prefab != nullptr) ? prefab->ReadData(u8"scene")
                                                       : UniquePtr<IStream>{};
                        } });
                dscene::ResolveSceneResources(*m_scene, *m_resources);
            }
            EnsureCamera();

            m_scene->Start();
            m_scene->SetSimulationEnabled(true);
            DRACONIC_LOG_INFO(u8"Player", u8"running scene '{}'", scenePath);

            // The project's default input map: cooked resource -> the input subsystem's
            // ActionRuntime. Nil/unresolved = the runtime simply has no actions bound.
            if (m_input != nullptr && !m_settings.defaultInputMapId.IsNil())
            {
                auto mapProxy = m_resources->Bind<draconic::input::InputMapResource>(
                    m_settings.defaultInputMapId);
                if (mapProxy)
                {
                    m_input->SetMap(mapProxy->Map());
                    DRACONIC_LOG_INFO(u8"Player", u8"input map bound ({} set(s))",
                                      mapProxy->Map().sets.Size());
                }
                else
                {
                    DRACONIC_LOG_WARNING(u8"Player", u8"default input map did not resolve");
                }
            }

            StartGameScript(host);
        }

        void OnUpdate(rt::IApplicationHost& host, f32 deltaTime) override
        {
            rt::DefaultApplication::OnUpdate(host, deltaTime);
            if (m_game.Get() != nullptr)
            {
                // Gameplay time: the script's update(dt) sees the SCALED clock.
                Variant dt = Variant::From(deltaTime * host.Ctx().TimeScale());
                if (auto result = m_game->Invoke(u8"update", Span<Variant>{ &dt, 1 }); !result.HasValue())
                {
                    DRACONIC_LOG_ERROR(u8"Player", u8"game script update() faulted - stopping script");
                    m_game = nullptr;
                }
            }
            if (m_options.exitAfterSeconds > 0.0f)
            {
                m_elapsed += deltaTime;
                if (m_elapsed >= m_options.exitAfterSeconds) { host.RequestExit(0); }
            }
        }

        void OnExit(rt::IApplicationHost&) override
        {
            if (m_game.Get() != nullptr)
            {
                (void)m_game->Invoke(u8"exit", Span<Variant>{});
                m_game = nullptr;
            }
            m_scriptContext = nullptr;
            m_scriptManager = nullptr;
            if (m_scene != nullptr) { m_scene->Stop(); }
        }

        void OnShutdown(rt::IApplicationHost&) override
        {
            m_resources = nullptr;   // release products while the device is alive
        }

    private:
        // A renderable scene needs a primary camera; authored game scenes should carry one, but
        // a bare editor scene shouldn't ship a black screen - frame the origin like the editor does.
        void EnsureCamera()
        {
            auto* cameras = m_scene->GetSystem<draconic::render::CameraComponentManager>();
            if (cameras == nullptr) { return; }
            bool hasCamera = false;
            cameras->ForEach([&](draconic::render::CameraComponent&, dscene::EntityHandle) { hasCamera = true; });
            if (hasCamera) { return; }

            DRACONIC_LOG_WARNING(u8"Player", u8"scene has no camera - adding a default one");
            const dscene::EntityHandle e = m_scene->CreateEntity(u8"PlayerCamera");
            Transform t;
            t.position = Float3{ 8.0f, 6.0f, 10.0f };
            // Yaw toward the origin, then pitch down (same convention as the seeded Sun).
            t.rotation = Quaternion::FromAxisAngle(Float3{ 0, 1, 0 }, 0.675f)
                       * Quaternion::FromAxisAngle(Float3{ 1, 0, 0 }, -0.42f);
            m_scene->SetLocalTransform(e, t);
            cameras->Add(e);
        }

        // The GAME SCRIPT: a Wren class `Game` with construct new(), launch(), update(dt),
        // exit() - all optional except the class itself. Faults disable the script, not the game.
        void StartGameScript(rt::IApplicationHost&)
        {
            const StringView scriptPath = m_settings.startupScript.AsView();
            if (scriptPath.IsEmpty()) { return; }

            // Project mode: a loose file under the project root. Dist mode: a raw pak entry.
            draconic::vfs::IFileSystem& root = (m_pak.Get() != nullptr)
                ? static_cast<draconic::vfs::IFileSystem&>(*m_pak)
                : static_cast<draconic::vfs::IFileSystem&>(*m_root);
            UniquePtr<IStream> stream = root.Open(scriptPath, FileMode::Read);
            if (!stream)
            {
                DRACONIC_LOG_ERROR(u8"Player", u8"startup script '{}' not found", scriptPath);
                return;
            }
            Array<byte> bytes;
            bytes.Resize(static_cast<usize>(stream->Size()));
            if (stream->Read(bytes.Data(), bytes.Size()) != bytes.Size())
            {
                DRACONIC_LOG_ERROR(u8"Player", u8"startup script '{}' unreadable", scriptPath);
                return;
            }
            const StringView source(reinterpret_cast<const utf8char*>(bytes.Data()), bytes.Size());

            draconic::input::RegisterInputScriptApi();   // scripts get the Input facade
            draconic::physics::RegisterPhysicsScriptApi();   // ...and the Physics facade
            m_scriptManager = draconic::script::wren::CreateScriptManager();
            draconic::script::RegisterReflectedTypes(*m_scriptManager);
            m_scriptContext = m_scriptManager->CreateContext();
            if (m_input != nullptr) { m_input->ExposeToScript(*m_scriptContext); }
            if (m_physics != nullptr) { m_physics->ExposeToScript(*m_scriptContext); }
            if (!m_scriptContext->Load(source, scriptPath).IsOk())
            {
                DRACONIC_LOG_ERROR(u8"Player", u8"startup script '{}' failed to compile", scriptPath);
                return;
            }
            m_game = m_scriptContext->CreateInstance(u8"Game", Span<Variant>{});
            if (m_game.Get() == nullptr)
            {
                DRACONIC_LOG_ERROR(u8"Player", u8"startup script has no `Game` class (construct new())");
                return;
            }
            (void)m_game->Invoke(u8"launch", Span<Variant>{});
            DRACONIC_LOG_INFO(u8"Player", u8"game script '{}' launched", scriptPath);
        }

        PlayerOptions m_options;
        f32 m_elapsed = 0.0f;
        draconic::project::ProjectSettings m_settings;
        draconic::input::InputSubsystem* m_input = nullptr;
        draconic::physics::PhysicsSubsystem* m_physics = nullptr;
        draconic::input::InputMapFactory m_inputMapFactory;
        draconic::physics::CollisionShapeFactory m_collisionShapeFactory;
        draconic::physics::PhysicalMaterialFactory m_physicalMaterialFactory;
        UniquePtr<draconic::vfs::NativeFileSystem> m_root;
        UniquePtr<draconic::vfs::PakFileSystem> m_pak;             // dist mode only
        UniquePtr<draconic::vfs::NativeFileSystem> m_contentMount; // project mode only
        UniquePtr<draconic::vfs::NativeFileSystem> m_cookedMount;
        UniquePtr<draconic::content::ContentDatabase> m_sourceDb;  // project mode: authored scenes
        UniquePtr<draconic::content::ContentDatabase> m_contentDb; // products (and dist scenes)
        draconic::content::ContentDatabase* m_sceneDb = nullptr;   // where scenes come from
        UniquePtr<res::ResourceManager> m_resources;
        draconic::geometry::StaticMeshFactory m_meshFactory;
        draconic::geometry::SkinnedMeshFactory m_skinnedFactory;
        draconic::materials::MaterialFactory m_materialFactory;
        draconic::animation::SkeletonFactory m_skeletonFactory;
        draconic::animation::AnimationClipFactory m_clipFactory;
        draconic::animation::AnimationGraphFactory m_graphFactory;
        draconic::particles::ParticleEffectFactory m_effectFactory;
        UniquePtr<draconic::texture::TextureFactory> m_textureFactory;
        dscene::Scene* m_scene = nullptr;   // owned by the SceneSubsystem
        RefPtr<draconic::script::IScriptManager> m_scriptManager;
        RefPtr<draconic::script::IScriptContext> m_scriptContext;
        RefPtr<draconic::script::ScriptObject> m_game;
    };
}

int main(int argc, char** argv)
{
    ConsoleSink consoleSink;
    GlobalLogger().AddSink(&consoleSink);
    GlobalLogger().SetMinLevel(LogLevel::Info);

    PlayerOptions options;
    if (argc > 1 && argv[1][0] != '-')
    {
        options.projectDir = String(StringView(reinterpret_cast<const utf8char*>(argv[1])));
    }
    else
    {
        // No path given: behave like a SHIPPED game binary - the game is wherever we are.
        // Try the current directory, then the executable's own directory (double-click /
        // run-from-anywhere), then the dev default.
        namespace fs = std::filesystem;
        std::error_code ec;
        auto hasGame = [](const fs::path& dir) {
            std::error_code e;
            return fs::exists(dir / "Content.pak", e) || fs::exists(dir / "Project.xml", e);
        };
        const fs::path exeDir =
            fs::weakly_canonical(fs::absolute(fs::path(argv[0]), ec), ec).parent_path();
        fs::path chosen = ".";
        if (!hasGame(chosen) && hasGame(exeDir)) { chosen = exeDir; }
        else if (!hasGame(chosen)) { chosen = "EditorProject"; }   // dev fallback
        options.projectDir = String(StringView(
            reinterpret_cast<const utf8char*>(chosen.string().c_str())));
    }
    for (int i = 1; i < argc - 1; ++i)
    {
        if (std::strcmp(argv[i], "--scene") == 0)
        {
            options.sceneOverride = String(StringView(reinterpret_cast<const utf8char*>(argv[i + 1])));
        }
        if (std::strcmp(argv[i], "--exit-after") == 0)
        {
            options.exitAfterSeconds = static_cast<f32>(std::atof(argv[i + 1]));
        }
    }

    shell::WindowSettings ws;
    ws.title  = u8"Raptor Player";
    ws.width  = 1280;
    ws.height = 720;
    auto shellPtr = shell::CreateShell(ws);
    if (shellPtr.Get() == nullptr || shellPtr->MainWindow() == nullptr)
    {
        std::fprintf(stderr, "RaptorPlayer: failed to create the OS shell/window\n");
        return 1;
    }

    graphics::GraphicsDeviceDesc gdd;
    gdd.backend = graphics::BackendType::Vulkan;
    auto gpu = graphics::CreateGraphicsDevice(gdd);
    if (!gpu.HasValue())
    {
        std::fprintf(stderr, "RaptorPlayer: failed to create the graphics device\n");
        return 1;
    }

    PlayerApplication app(static_cast<PlayerOptions&&>(options));
    const int code = rt::RunApplication(app, *shellPtr, gpu.Value().Get());
    GlobalLogger().RemoveSink(&consoleSink);
    return code;
}
