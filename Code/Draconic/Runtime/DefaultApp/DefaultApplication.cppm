// Draconic::RuntimeDefaultApp - the `draconic.runtime.defaultapp` module.
//
// DefaultApplication: an opinionated IApplication base that registers the standard
// engine subsystems. A game that wants the batteries-included engine writes
// `class MyGame : DefaultApplication` and adds its own subsystems in Configure
// (calling the base first); a game that wants only its own subsystems implements
// IApplication directly and links none of this.
//
// This lives in its OWN library - separate from draconic.runtime.client - precisely
// so the base client never pulls in the engine subsystem libraries. It registers ALL
// standard gameplay subsystems (runtime-host.md v3: the editor embeds THIS same class
// against its runtime context, so subsystem registration lives here, not in entry
// points) and owns the GAME-SCRIPT lifecycle (the Wren `Game` class bracket) - the
// player and the editor's Game tab both consume it instead of hand-rolling copies.

module;
#include "Core/Prelude.h"
#include "Core/Log/Log.h"

export module draconic.runtime.defaultapp;

import draconic.core;
import draconic.rhi;
import draconic.runtime.client;     // IApplication, IApplicationHost
import draconic.runtime.gameinstance; // GameInstance - this app's running game (scene + script bracket)
import draconic.shell;   // IShell, IKeyboard, KeyCode (the profile-dump hotkey)
import draconic.graphics;   // GraphicsDevice, FrameContext
import draconic.scene;              // Scene
import draconic.scene.subsystem;    // SceneSubsystem (the standard scene driver)
import draconic.render.subsystem;   // RenderSubsystem (the standard renderer)
import draconic.animation.subsystem; // AnimationSubsystem (drives skeletal animation from the scene)
import draconic.particles.subsystem; // ParticleSubsystem (scene-driven CPU sim)
import draconic.physics;             // ContactKind/EntityContact (the contact bridge)
import draconic.physics.subsystem;  // PhysicsSubsystem (Jolt worlds + interpolation)
import draconic.input;              // the action model/runtime
import draconic.input.subsystem;    // InputSubsystem + the Wren Input facade
import draconic.script;             // IScriptManager/Context (the game script)
import draconic.script.wren;        // the Wren backend
import draconic.script.angelscript; // the AngelScript backend (opt-in second backend)
import draconic.script.resource;    // cooked script classes + factory (entity behaviors)
import draconic.script.subsystem;   // ScriptSubsystem (behaviors + the run's shared context)
import draconic.resource;           // ResourceManager (owned or borrowed - see the preset seam)
import draconic.content;            // IContentDatabase (preset by the entry point)
import draconic.scene.resource;     // SceneDocument (product-type registration)
import draconic.geometry.resource;  // mesh factories
import draconic.materials.resource; // material factory
import draconic.animation.resource; // skeleton/clip/graph factories
import draconic.particles.resource; // particle-effect factory
import draconic.input.resource;     // input-map factory
import draconic.physics.resource;   // collision-shape/physical-material factories
import draconic.texture.resource;   // texture factory (device-backed)
import draconic.image.resource;     // image resource registration
import draconic.model.resource;     // cooked-model family types + registration
import draconic.ui.resource;        // cooked UI documents/themes (game-ui)
import draconic.ui.subsystem;       // the game screen tier (canvases + overlay + consumption)
import draconic.audio;              // AudioEngine (owned by the audio subsystem)
import draconic.audio.resource;     // cooked audio clips + factory
import draconic.audio.subsystem;    // AudioSubsystem (voices/buses/one-shots + scene sync)
import draconic.net;                // UdpSocket / DatagramEndpoint (the transport)
import draconic.net.replication;    // NetworkId / StateReplication (the spawn-handler seam)
import draconic.net.subsystem;      // NetSubsystem + NetworkStartup/StartNetworking + the Net facade
import draconic.profiler;           // the CPU scope profiler (P-key dump)

namespace rhi = draconic::rhi;
namespace core  = draconic::core;
namespace net = draconic::net;   // NetSubsystem + NetworkStartup + the Net facade
using namespace draconic::shell;   // IShell + input/window types (moved from draconic::runtime)
using namespace draconic::graphics;   // GraphicsDevice/RenderWindow/FrameContext (moved from draconic::runtime)

export namespace draconic::runtime
{
    class DefaultApplication : public IApplication
    {
    public:
        // Press P to print the previous frame's CPU scope tree + per-pass GPU timing. A game
        // subclass that overrides OnUpdate should call DefaultApplication::OnUpdate(host, dt) to
        // keep the hotkey. (Reads the GPU timestamps after a device stall - fine for an on-demand dump.)
        void OnUpdate(IApplicationHost& host, core::f32 deltaTime) override
        {
            TickGameScript(host, deltaTime);
            IShell* plat = host.Shell();
            IInputManager* input = (plat != nullptr) ? plat->Input() : nullptr;
            IKeyboard* kb = (input != nullptr) ? input->Keyboard() : nullptr;
            if (kb == nullptr || !kb->IsKeyPressed(KeyCode::P)) { return; }

            core::ConsoleWrite(draconic::profiler::Profiler::Get().BuildReport().AsView());
            if (auto* renderer = host.Ctx().GetSubsystem<draconic::render::RenderSubsystem>())
            {
                core::String gpu;
                renderer->BuildGpuProfileReport(gpu);
                core::ConsoleWrite(gpu.AsView());
            }
        }

        // Ticks the game script with GAMEPLAY time: dt x context scale x the primary
        // scene's scale (per-scene time, H1). Subclasses overriding OnUpdate call the
        // base to keep the script (and the profile hotkey) alive.
        void TickGameScript(IApplicationHost& host, core::f32 deltaTime)
        {
            m_instance.TickScript(deltaTime, host.Ctx().TimeScale());
        }

        // Registers ALL standard engine subsystems. A game subclass overrides this,
        // calls DefaultApplication::Configure(host) first, then adds its own. Entry
        // points (player, editor) do NOT register gameplay subsystems - this is the
        // one place (runtime-host.md v3).
        void Configure(IApplicationHost& host) override
        {
            host.Ctx().AddSubsystem<draconic::scene::SceneSubsystem>();
            if (GraphicsDevice* gfx = host.Graphics(); gfx != nullptr && gfx->Raw() != nullptr)
            {
                host.Ctx().AddSubsystem<draconic::render::RenderSubsystem>(*gfx->Raw(), gfx->FramesInFlight());
                // Drives skeletal animation from the scene tick (injects the SkeletalAnimation manager,
                // ticks players, feeds bone matrices to mesh components). Needs the render managers.
                host.Ctx().AddSubsystem<draconic::animation::AnimationSubsystem>();
                host.Ctx().AddSubsystem<draconic::particles::ParticleSubsystem>();
            }
            m_physics = host.Ctx().AddSubsystem<draconic::physics::PhysicsSubsystem>();
            m_audio = host.Ctx().AddSubsystem<draconic::audio::AudioSubsystem>(m_audioEngineSettings);
            m_input = host.Ctx().AddSubsystem<draconic::input::InputSubsystem>(
                host.Shell() != nullptr ? host.Shell()->Input() : nullptr);
            m_ui = host.Ctx().AddSubsystem<draconic::ui::UISubsystem>();
            if (!m_uiFontPath.IsEmpty()) { m_ui->SetFontPath(m_uiFontPath.AsView()); }

            // Entity behaviors (scripting.md P1). Facade/backend registration is
            // batteries-included here (idempotent - entry points may register more);
            // the run context itself is created lazily by the subsystem and SHARED
            // with the game script (one gameplay context per run, the locked rule).
            m_scripts = host.Ctx().AddSubsystem<draconic::script::ScriptSubsystem>();
            // The run's script host lives on the GameInstance now (game-instance.md §11): lend it to
            // the subsystem so the game script + this instance's scenes' behaviors share the one
            // context. At N=1 this is behaviour-identical to the subsystem owning it.
            m_scripts->UseRunHost(&m_instance.RunHost());
            draconic::input::RegisterInputScriptApi();
            draconic::physics::RegisterPhysicsScriptApi();
            draconic::audio::RegisterAudioScriptApi();
            // Both backends are registered (batteries-included); a run resolves by the
            // script's language - one gameplay context per run stays the locked rule.
            draconic::script::wren::RegisterWrenScriptBackend();
            draconic::script::angelscript::RegisterAngelScriptBackend();
            // Networking (net.md §6): open the socket + enter the role from the preset config
            // (no-op for single-player). Registers the Net facade as a side effect when active;
            // the context configurator installs its per-context service below.
            m_net = net::StartNetworking(m_netStartup);
            if (m_netStartup.role != net::NetworkRole::None &&
                (!m_net.socket || !m_net.socket->IsOpen()))
            {
                DRACONIC_LOG_ERROR(u8"App", u8"networking failed to open a socket (port {}) - running offline",
                                   m_netStartup.listenPort);
            }

            DefaultApplication* self = this;

            // Client-side network spawn: a replicated prefab id -> a live prefab instance from the
            // content DB (mirrors the script Scene.spawn resolver; replication then applies the
            // transform + other fields on top). The server assigns ids; game rules set relevancy.
            if (m_net.IsActive())
            {
                m_net.subsystem->Replication().SetSpawnHandler(
                    core::Function<draconic::scene::EntityHandle(draconic::scene::Scene&,
                        const core::Guid&, net::NetworkId)>{
                        [self](draconic::scene::Scene& scene, const core::Guid& prefabId,
                               net::NetworkId) -> draconic::scene::EntityHandle {
                            if (self->m_contentDatabase == nullptr) { return draconic::scene::EntityHandle::Invalid(); }
                            draconic::content::Instance* prefab = self->m_contentDatabase->GetInstance(prefabId);
                            core::UniquePtr<core::IStream> payload = (prefab != nullptr)
                                ? prefab->ReadData(u8"scene") : core::UniquePtr<core::IStream>{};
                            if (!payload) { return draconic::scene::EntityHandle::Invalid(); }
                            const draconic::scene::EntityHandle root =
                                draconic::scene::SpawnPrefab(scene, *payload, prefabId);
                            if (root.IsAssigned() && self->Resources() != nullptr)
                            {
                                draconic::scene::ResolveSceneResources(scene, *self->Resources());
                            }
                            return root;
                        } });
            }
            m_scripts->SetContextConfigurator(
                core::Function<void(draconic::script::IScriptContext&)>{
                    [self](draconic::script::IScriptContext& context) {
                        if (self->m_input != nullptr) { self->m_input->ExposeToScript(context); }
                        if (self->m_physics != nullptr) { self->m_physics->ExposeToScript(context); }
                        if (self->m_audio != nullptr)
                        {
                            self->m_audio->ExposeToScript(context, self->Resources());
                        }
                        if (self->m_net.IsActive()) { self->m_net.subsystem->InstallScriptService(context); }
                    } });
            // Scene.spawn: resolve the prefab payload from the content DB the entry point
            // preset, spawn it, place the root at the requested world position, and bind
            // the freshly spawned entities' resources.
            m_scripts->SetPrefabSpawner(
                core::Function<draconic::scene::EntityHandle(draconic::scene::Scene*,
                    const core::Guid&, const core::Float3&)>{
                    [self](draconic::scene::Scene* scene, const core::Guid& prefabId,
                           const core::Float3& position) -> draconic::scene::EntityHandle {
                        if (scene == nullptr || self->m_contentDatabase == nullptr)
                        {
                            return draconic::scene::EntityHandle::Invalid();
                        }
                        draconic::content::Instance* prefab =
                            self->m_contentDatabase->GetInstance(prefabId);
                        core::UniquePtr<core::IStream> payload = (prefab != nullptr)
                            ? prefab->ReadData(u8"scene") : core::UniquePtr<core::IStream>{};
                        if (!payload) { return draconic::scene::EntityHandle::Invalid(); }
                        const draconic::scene::EntityHandle root =
                            draconic::scene::SpawnPrefab(*scene, *payload, prefabId);
                        if (root.IsAssigned())
                        {
                            core::Transform transform = scene->GetLocalTransform(root);
                            transform.position = position;
                            scene->SetLocalTransform(root, transform);
                            if (self->Resources() != nullptr)
                            {
                                draconic::scene::ResolveSceneResources(*scene, *self->Resources());
                            }
                        }
                        return root;
                    } });

            // Composition-root bridge: forward physics contacts to the script subsystem's
            // neutral ingress. Keeps the two subsystems independent - neither depends on the
            // other for scripting; the wiring lives here, where integration belongs.
            if (m_physics != nullptr && m_scripts != nullptr)
            {
                m_contactBridge.scripts = m_scripts;
                m_physics->RegisterContactListener(&m_contactBridge);
            }
        }

        [[nodiscard]] draconic::script::ScriptSubsystem* Scripts() const noexcept
        {
            return m_scripts;
        }

        [[nodiscard]] draconic::input::InputSubsystem* Input() const noexcept { return m_input; }
        [[nodiscard]] draconic::physics::PhysicsSubsystem* Physics() const noexcept { return m_physics; }
        [[nodiscard]] draconic::audio::AudioSubsystem* Audio() const noexcept { return m_audio; }

        /// Preset BEFORE Configure: enter a server/client role at startup (default = single-player,
        /// no socket). The player's launch flow / editor Game tab fills this from project settings.
        void SetNetworkStartup(const net::NetworkStartup& startup) { m_netStartup = startup; }
        [[nodiscard]] net::NetSubsystem* Net() const noexcept { return m_net.subsystem.Get(); }

        // Drives the network on the FIXED lane (deterministic step): pump incoming datagrams,
        // dispatch RPCs, flush reliable sends. Runs even with no game script (a dedicated server
        // has none). A subclass overriding OnFixedUpdate calls the base to keep the network alive.
        void OnFixedUpdate(IApplicationHost& host, core::f32 fixedDeltaTime) override
        {
            (void)host;
            if (m_net.IsActive()) { m_net.subsystem->Update(fixedDeltaTime * 1000.0f); }   // seconds -> ms
        }
        /// Preset BEFORE Configure: audio engine tuning (listener count for split-screen,
        /// voice pool sizes). Defaults suit a single-listener game.
        void SetAudioEngineSettings(const draconic::audio::AudioEngineSettings& settings)
        {
            m_audioEngineSettings = settings;
        }
        [[nodiscard]] draconic::ui::UISubsystem* UI() const noexcept { return m_ui; }

        /// TTF for the game UI's default font (preset BEFORE Configure; the editor passes
        /// its own font path, the player defaults to the dev-tree Roboto).
        void SetUIFontPath(core::StringView path) { m_uiFontPath = core::String(path); }

        // ---- infrastructure preset (Sedulous PresetInfrastructure lineage): shared
        // pieces are handed in BEFORE Startup; anything not preset the app creates for
        // itself and owns. The editor presets its EXISTING manager (a second manager
        // over the same cooked DB would load every product twice); the player presets
        // its cooked DB and lets the app build the manager. ----

        /// Borrow an existing manager (editor). Wins over SetContentDatabase.
        void SetResourceManager(draconic::resource::ResourceManager* borrowed) noexcept
        {
            m_borrowedResources = borrowed;
        }
        /// The cooked-content database the app should build its OWN manager over (player).
        void SetContentDatabase(draconic::content::IContentDatabase* database) noexcept
        {
            m_contentDatabase = database;
        }
        [[nodiscard]] draconic::resource::ResourceManager* Resources() const noexcept
        {
            return m_borrowedResources != nullptr ? m_borrowedResources : m_ownedResources.Get();
        }

        // Registers the runtime product types + the STANDARD resource factories into the
        // preset/created manager. Subclasses overriding OnStartup call the base AFTER
        // presetting the database/manager.
        void OnStartup(IApplicationHost& host) override
        {
            // Product/runtime types: factories construct cooked products BY TYPE NAME.
            draconic::model::RegisterModelResourceTypes();
            draconic::image::RegisterImageResource();
            draconic::particles::RegisterParticleEffectResource();
            draconic::input::RegisterInputMapResource();
            draconic::physics::RegisterPhysicsResource();
            draconic::audio::RegisterAudioResource();
            draconic::script::RegisterScriptResource();
            draconic::ui::RegisterUIResource();
            core::GlobalTypeRegistry().Register(draconic::scene::SceneDocument::StaticType());
            core::RegisterSerializable<draconic::scene::SceneDocument>();
            draconic::ui::RegisterUIComponentReflection();
            if (GraphicsDevice* gfx = host.Graphics(); gfx != nullptr && gfx->Raw() != nullptr
                && m_ui != nullptr)
            {
                m_ui->EnsureRenderReady(*gfx->Raw(), gfx->FramesInFlight());
            }

            if (m_borrowedResources == nullptr && m_contentDatabase != nullptr)
            {
                m_ownedResources = core::MakeUnique<draconic::resource::ResourceManager>(
                    core::DefaultAllocator(), *m_contentDatabase);
            }
            draconic::resource::ResourceManager* resources = Resources();
            if (resources == nullptr) { return; }   // headless/no-content apps
            resources->AddFactory(&m_meshFactory);
            resources->AddFactory(&m_skinnedMeshFactory);
            resources->AddFactory(&m_materialFactory);
            resources->AddFactory(&m_skeletonFactory);
            resources->AddFactory(&m_animationClipFactory);
            resources->AddFactory(&m_animationGraphFactory);
            resources->AddFactory(&m_particleEffectFactory);
            resources->AddFactory(&m_inputMapFactory);
            resources->AddFactory(&m_collisionShapeFactory);
            resources->AddFactory(&m_physicalMaterialFactory);
            resources->AddFactory(&m_audioClipFactory);
            resources->AddFactory(&m_busLayoutFactory);
            resources->AddFactory(&m_soundCueFactory);
            resources->AddFactory(&m_scriptClassFactory);
            resources->AddFactory(&m_modelFactory);
            resources->AddFactory(&m_uiDocumentFactory);
            resources->AddFactory(&m_uiThemeFactory);
            if (GraphicsDevice* gfx = host.Graphics(); gfx != nullptr && gfx->Raw() != nullptr)
            {
                m_textureFactory = core::MakeUnique<draconic::texture::TextureFactory>(
                    core::DefaultAllocator(), *gfx->Raw());
                resources->AddFactory(m_textureFactory.Get());
            }
        }

        void OnShutdown(IApplicationHost&) override
        {
            if (m_physics != nullptr) { m_physics->UnregisterContactListener(&m_contactBridge); }
            m_net.subsystem = nullptr;   // stop the session (drops peers) before closing the socket
            m_net.socket = nullptr;
            m_ownedResources = nullptr;   // release products while the device is alive
            m_textureFactory = nullptr;
        }

        // ---- the game script (a Wren class `Game`: construct new(), launch(), update(dt),
        // exit() - all optional except the class). Faults disable the SCRIPT, not the game. ----

        /// The scene whose time scale the script's update(dt) follows (and, later, the
        /// scene game services bind against). Set by the launch flow; null = context time.
        void SetPrimaryScene(draconic::scene::Scene* scene) noexcept
        {
            m_instance.SetScene(scene);
            // The primary gameplay scene is the replicated world (server captures / client applies).
            if (m_net.IsActive()) { m_net.subsystem->SetReplicatedScene(scene); }
        }
        [[nodiscard]] draconic::scene::Scene* PrimaryScene() const noexcept { return m_instance.GetScene(); }

        /// Optional per-run error sink (the editor surfaces notices); set BEFORE
        /// StartGameScript, cleared automatically on StopGameScript.
        void SetGameScriptErrorHandler(draconic::script::IScriptErrorHandler* handler) noexcept
        {
            m_instance.SetScriptErrorHandler(handler);
        }

        /// Compiles + launches the game script from source text - delegated to the run's GameInstance.
        /// The CALLER resolves where the source lives (player: project file / pak entry; editor:
        /// SourceDb). The exposeServices lambda binds the per-context script facades on the fallback
        /// path (the normal path uses the ScriptSubsystem's configured shared context).
        bool StartGameScript(core::StringView source, core::StringView name)
        {
            DefaultApplication* self = this;
            return m_instance.StartScript(m_scripts,
                core::Function<void(draconic::script::IScriptContext&)>{
                    [self](draconic::script::IScriptContext& context) {
                        if (self->m_input != nullptr) { self->m_input->ExposeToScript(context); }
                        if (self->m_physics != nullptr) { self->m_physics->ExposeToScript(context); }
                        if (self->m_audio != nullptr) { self->m_audio->ExposeToScript(context, self->Resources()); }
                        if (self->m_net.IsActive()) { self->m_net.subsystem->InstallScriptService(context); }
                    } },
                source, name);
        }

        /// exit() + teardown (idempotent; the update fault path also lands here).
        void StopGameScript() { m_instance.StopScript(m_scripts); }
        [[nodiscard]] bool GameScriptRunning() const noexcept { return m_instance.ScriptRunning(); }

        // Default render: draw every active scene into the window via the RenderSubsystem.
        // A game overrides this for custom rendering. (Single-scene for now - multiple
        // active scenes would each clear; compositing is a later concern.)
        void OnRenderWindow(IApplicationHost& host, FrameContext& frame) override
        {
            auto* render = host.Ctx().GetSubsystem<draconic::render::RenderSubsystem>();
            auto* scenes = host.Ctx().GetSubsystem<draconic::scene::SceneSubsystem>();
            if (render == nullptr || !render->IsReady() || scenes == nullptr ||
                frame.encoder == nullptr || frame.backbufferView == nullptr || frame.window == nullptr)
            {
                frame.Clear(0.08f, 0.09f, 0.12f, 1.0f);   // no renderer - present a clear
                return;
            }

            const rhi::TextureFormat colorFormat = frame.window->Swap()->Format();
            // RenderTexture canvases draw BEFORE the scene so materials sampling them
            // see this frame's UI (the RenderCanvasTextures host seam).
            if (m_ui != nullptr)
            {
                m_ui->RenderCanvasTextures(*frame.encoder, static_cast<core::i32>(frame.frameIndex));
            }
            render->BeginRendering(*frame.encoder, frame.frameIndex);
            for (draconic::scene::Scene* scene : scenes->ActiveScenes())
            {
                render->RenderScene(*scene, frame.backbufferView, colorFormat,
                                    frame.width, frame.height);   // clear comes from the scene's camera
            }
            render->EndRendering();   // scene-tier overlays (HUD/billboards) draw inside the compose

            // Window-space overlays (screen-tier UI, diagnostics, ...) composite over the
            // finished frame through the generic registry - the host names no source.
            render->RenderOverlays(*frame.encoder, frame.backbufferView, colorFormat,
                                   frame.width, frame.height, frame.frameIndex);
        }

    private:
        // Bridges physics contacts to the script subsystem's neutral ingress, mapping the
        // physics kind onto the script vocabulary. This is the ONLY place physics and script
        // meet for contacts - the subsystems stay mutually independent.
        struct ScriptContactBridge final : public draconic::physics::IContactListener
        {
            draconic::script::ScriptSubsystem* scripts = nullptr;
            void OnContact(const draconic::physics::EntityContact& c) override
            {
                if (scripts == nullptr) { return; }
                using SK = draconic::script::ScriptContactKind;
                SK kind = SK::Begin;
                switch (c.kind)
                {
                    case draconic::physics::ContactKind::Begin:        kind = SK::Begin; break;
                    case draconic::physics::ContactKind::End:          kind = SK::End; break;
                    case draconic::physics::ContactKind::TriggerEnter: kind = SK::TriggerEnter; break;
                    case draconic::physics::ContactKind::TriggerExit:  kind = SK::TriggerExit; break;
                }
                scripts->DeliverContact(c.scene, c.a, c.b, kind, c.point, c.normal, c.speed);
            }
        };
        ScriptContactBridge m_contactBridge;

        draconic::geometry::StaticMeshFactory m_meshFactory;
        draconic::geometry::SkinnedMeshFactory m_skinnedMeshFactory;
        draconic::materials::MaterialFactory m_materialFactory;
        draconic::animation::SkeletonFactory m_skeletonFactory;
        draconic::animation::AnimationClipFactory m_animationClipFactory;
        draconic::animation::AnimationGraphFactory m_animationGraphFactory;
        draconic::particles::ParticleEffectFactory m_particleEffectFactory;
        draconic::input::InputMapFactory m_inputMapFactory;
        draconic::physics::CollisionShapeFactory m_collisionShapeFactory;
        draconic::physics::PhysicalMaterialFactory m_physicalMaterialFactory;
        draconic::audio::AudioClipFactory m_audioClipFactory;
        draconic::audio::AudioBusLayoutFactory m_busLayoutFactory;
        draconic::audio::SoundCueFactory m_soundCueFactory;
        draconic::script::ScriptClassFactory m_scriptClassFactory;
        draconic::audio::AudioEngineSettings m_audioEngineSettings;
        draconic::model::ModelFactory m_modelFactory;
        draconic::ui::UIDocumentFactory m_uiDocumentFactory;
        draconic::ui::UIThemeFactory m_uiThemeFactory;
        core::UniquePtr<draconic::texture::TextureFactory> m_textureFactory;
        draconic::resource::ResourceManager* m_borrowedResources = nullptr;
        draconic::content::IContentDatabase* m_contentDatabase = nullptr;
        core::UniquePtr<draconic::resource::ResourceManager> m_ownedResources;
        draconic::input::InputSubsystem* m_input = nullptr;
        draconic::ui::UISubsystem* m_ui = nullptr;
        core::String m_uiFontPath;
        draconic::physics::PhysicsSubsystem* m_physics = nullptr;
        draconic::audio::AudioSubsystem* m_audio = nullptr;
        net::NetworkStartup m_netStartup;   // preset before Configure (default = single-player)
        net::NetworkRuntime m_net;          // socket + subsystem; subsystem destructs first (declared after socket)
        draconic::script::ScriptSubsystem* m_scripts = nullptr;
        GameInstance m_instance;   // this app's single running game (scene + script; Array in a later phase)
    };
}
