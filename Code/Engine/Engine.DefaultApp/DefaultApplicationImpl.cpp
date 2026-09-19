// SPDX-License-Identifier: MIT
// Copyright (c) 2026-Present Robert Campbell

// Runtime - engine.defaultapp implementation unit.
//
// Out-of-line definitions for DefaultApplication's member functions (sec 3.2 / sec 10.6).
// The class declaration + trivial inline accessors stay in DefaultApplication.cppm.

module;
#include "Core/Prelude.h"
#include "Core/Log/Log.h"

module engine.defaultapp;

import foundation.core;
import foundation.rhi;
import foundation.runtime.client;       // IApplication, IApplicationHost
import engine.gameinstance; // GameInstance - this app's running game (scene + script bracket)
import foundation.shell;                // IShell, IKeyboard, KeyCode (the profile-dump hotkey)
import foundation.graphics;             // GraphicsDevice, FrameContext
import foundation.vfs;                  // ResolveDataRoot + NativeFileSystem (the data mount)
import foundation.scene;                // Scene
import engine.scene;      // SceneSubsystem (the standard scene driver)
import engine.scenesurface; // FullSceneComposition (the single source of truth for scene assembly)
import engine.scriptsurface; // RegisterAllScriptFacades (the single source of truth for the script surface)
import engine.render;     // RenderSubsystem (the standard renderer)
import engine.animation; // AnimationSubsystem (drives skeletal animation from the scene)
import engine.particles; // ParticleSubsystem (scene-driven CPU sim)
import engine.terrain;   // TerrainSubsystem (chunked geo-mipmap terrain renderer)
import foundation.physics;             // ContactKind/EntityContact (the contact bridge)
import engine.physics;   // PhysicsSubsystem (Jolt worlds + interpolation)
import engine.navigation; // NavigationSubsystem + script facade
import foundation.input;               // the action model/runtime
import engine.input;     // InputSubsystem + the Input facade
import foundation.script;              // IScriptManager/Context (the game script)
#ifdef OPTION_HAS_ANGELSCRIPT
import foundation.script.angelscript; // the AngelScript backend (second backend; OPTION_ENABLE_ANGELSCRIPT)
#endif
#ifdef OPTION_HAS_LUAU
import foundation.script.luau;         // the Luau backend (OPTION_ENABLE_LUAU)
#endif
import foundation.script.resource;     // cooked script classes + factory (entity behaviors)
import engine.script;    // ScriptSubsystem (behaviors + the run's shared context)
import foundation.resource;            // ResourceManager (owned or borrowed - see the preset seam)
import foundation.content;             // IContentDatabase (preset by the entry point)
import foundation.scene.resource;      // SceneDocument (product-type registration)
import foundation.geometry.resource;   // mesh factories
import foundation.materials.resource;  // material factory
import foundation.animation.resource;  // skeleton/clip/graph factories
import foundation.propertyanimation.resource; // property-animation clip factory
import foundation.particles.resource;  // particle-effect factory
import foundation.input.resource;      // input-map factory
import foundation.fonts.resource;      // FontResource + FontFactory (default UI font)
import foundation.physics.resource;    // collision-shape/physical-material factories
import foundation.navigation.resource; // navmesh-zone factory
import foundation.texture.resource;    // texture factory (device-backed)
import foundation.image;               // Image (the screenshot the capture hands back)
import foundation.image.resource;      // image resource registration
import foundation.model.resource;      // cooked-model family types + registration
import foundation.ui;                  // View (the `ui` binding's instantiate return type)
import foundation.ui.resource;         // cooked UI documents/themes (game-ui)
import engine.ui;        // the game screen tier (canvases + overlay + consumption)
import engine.ui.script;   // UiScreenScriptBinding + InstallUiScreenScriptService
import foundation.audio;               // AudioEngine (owned by the audio subsystem)
import foundation.audio.resource;      // cooked audio clips + factory
import engine.audio;     // AudioSubsystem (voices/buses/one-shots + scene sync)
import foundation.net;                 // UdpSocket / DatagramEndpoint (the transport)
import foundation.net.replication;     // NetworkId / StateReplication (the spawn-handler seam)
import foundation.net.manager;   // NetworkManager + NetworkStartup/StartNetworking + the Net facade
import engine.net; // NetworkSubsystem (injects the NetworkComponentManager into scenes)
import foundation.profiler;      // the CPU scope profiler (P-key dump)

namespace rhi = foundation::rhi;
namespace core = foundation::core;
namespace net = foundation::net;
using namespace foundation::runtime; // foundation runtime: IApplication/IApplicationHost/Subsystem/Context
using namespace foundation::shell;
using namespace foundation::graphics;
namespace scene = foundation::scene;

namespace engine::runtime
{
    void DefaultApplication::OnUpdate(IApplicationHost& host, core::f32 deltaTime)
    {
        // A screenshot recorded last frame: the GPU has run that frame by now for any slot the
        // host reuses, but not necessarily this one - a screenshot is a one-off, so wait for
        // everything, then map, write, and honour --screenshot-exit.
        if (m_screenshot.Recorded())
        {
            if (auto* gfx = host.Graphics(); gfx != nullptr && gfx->Raw() != nullptr)
            {
                gfx->Raw()->WaitIdle();
                foundation::image::Image written;
                (void)m_screenshot.Complete(*gfx->Raw(), host.Ctx().Allocator(), written);
            }
            if (m_screenshotExitPending)
            {
                host.RequestExit(0);
            }
        }
        m_runSeconds += deltaTime;
        if (m_exitAfterSeconds > 0.0f && m_runSeconds >= m_exitAfterSeconds)
        {
            host.RequestExit(0);
        }
        // Finalize any async resource loads first, so this frame's spawns/ticks see ready
        // resources (task #123). Pump ONLY the manager this app OWNS: when this DefaultApplication
        // is embedded in the editor it BORROWS the editor's manager (m_ownedResources stays null),
        // and the editor pumps that manager itself - pumping it here too would double-pump.
        // Inert until a factory migrates to the async path.
        if (m_ownedResources)
        {
            m_ownedResources->Pump();
        }

        // Drive + tick EVERY instance (primary + any extras - multi-instance PIE / headless server).
        // Input FIRST (so the game script sees this frame's keys), then the run host clock, then tick.
        const core::f32 contextScale = host.Ctx().TimeScale();
        ForEachInstance(
            [&](GameInstance& gi)
            {
                gi.PumpScriptLoads(); // activate any script-initiated load that finished (after Pump)
                gi.DriveInput(deltaTime, contextScale);
                gi.DriveRunHost(deltaTime);
                gi.TickScript(deltaTime, contextScale);
                gi.DrainRunEvents(); // deliver this frame's run-bus events (after the game script ticked)
            });
        IShell* plat = host.Shell();
        IInputManager* input = (plat != nullptr) ? plat->Input() : nullptr;
        IKeyboard* kb = (input != nullptr) ? input->Keyboard() : nullptr;
        if (kb != nullptr && kb->IsKeyPressed(KeyCode::F11))
        {
            // F11: a timestamped PNG in the working directory (the legacy sandbox binding).
            core::String path;
            core::AppendFormat(path, u8"screenshot_{}.png", core::Clock::Now().Ticks());
            CaptureScreenshot(path.AsView());
        }
        if (kb == nullptr || !kb->IsKeyPressed(KeyCode::P))
        {
            return;
        }

        core::ConsoleWrite(foundation::profiler::Profiler::Get().BuildReport().AsView());
        if (auto* renderer = host.Ctx().GetSubsystem<engine::render::RenderSubsystem>())
        {
            core::String gpu;
            renderer->BuildGpuProfileReport(gpu);
            core::ConsoleWrite(gpu.AsView());
        }
    }

    void DefaultApplication::TickGameScript(IApplicationHost& host, core::f32 deltaTime)
    {
        m_instance.TickScript(deltaTime, host.Ctx().TimeScale());
    }

    void DefaultApplication::Configure(IApplicationHost& host)
    {
        m_host = &host; // stable for the app's lifetime; extra instances route run.requestExit through it

        // THE data root, resolved once: the explicit override (validated) or the discovery walk.
        // Every consumer below reads through the mount - none knows the location. Missing = the
        // run ends with an error naming what was searched; the mount still points at the place a
        // dist would need (Data/ beside the executable) so every miss on the way out is explicit.
        m_dataRoot = foundation::vfs::ResolveDataRoot(m_dataRootOverride.AsView());
        if (m_dataRoot.IsEmpty())
        {
            LOG_ERROR(u8"App", u8"no data root - cannot start (expected Data/.dataroot beside the "
                               u8"executable or up the tree, or --data-root <dir>)");
            host.RequestExit(1);
            m_dataFileSystem = core::MakeUnique<foundation::vfs::NativeFileSystem>(
                host.Ctx().Allocator(),
                core::PathJoin(core::GetExecutableDirectory().AsView(), u8"Data").AsView(),
                host.Ctx().Allocator());
        }
        else
        {
            LOG_INFO(u8"App", u8"data root: {}", m_dataRoot);
            m_dataFileSystem = core::MakeUnique<foundation::vfs::NativeFileSystem>(
                host.Ctx().Allocator(), m_dataRoot.AsView(), host.Ctx().Allocator());
        }
        m_scenes = host.Ctx().AddSubsystem<engine::scene::SceneSubsystem>();
        // The scene-assembly blueprint: every registered manager's CreateScene
        // assembles from the full composition (the single source of truth).
        m_scenes->SetComposition(engine::FullSceneComposition());
        // The run's scene group lives on the GameInstance: register it so it
        // ticks on the Context lane. WireInstance centralizes this so extra instances wire the same way.
        m_scenes->RegisterManager(&m_instance.Scenes());
        // A script's scene.spawn reaches content through the scene's spawn system, which only the
        // app can point at the database and the manager - at every scene's SystemsReady.
        m_scenes->RegisterObserver(this, foundation::scene::SceneLifecycleStage::SystemsReady);
        if (GraphicsDevice* gfx = host.Graphics(); gfx != nullptr && gfx->Raw() != nullptr)
        {
            m_render = host.Ctx().AddSubsystem<engine::render::RenderSubsystem>(
                host.Ctx().Allocator(), *gfx->Raw(), gfx->FramesInFlight(), *m_dataFileSystem);
            // Drives skeletal animation from the scene tick (injects the SkeletalAnimation manager,
            // ticks players, feeds bone matrices to mesh components). Needs the render managers.
            host.Ctx().AddSubsystem<engine::animation::AnimationSubsystem>();
            host.Ctx().AddSubsystem<engine::particles::ParticleSubsystem>();
            // The chunked geo-mipmap terrain renderer: registers itself on Opaque + wires the
            // TerrainComponentManager (already injected by scene composition) as the render provider.
            host.Ctx().AddSubsystem<engine::terrain::TerrainSubsystem>();
        }
        m_physics = host.Ctx().AddSubsystem<engine::physics::PhysicsSubsystem>();
        host.Ctx().AddSubsystem<engine::navigation::NavigationSubsystem>();
        // Networking scene integration: injects the NetworkComponentManager into every scene so
        // authored NetworkComponents work (the per-instance endpoint replicates over it). The subsystem
        // also OWNS the per-frame transport pump: give it the endpoint enumerator (we own the
        // instance list; it owns the tick).
        auto* netSubsystem = host.Ctx().AddSubsystem<engine::net::NetworkSubsystem>();
        netSubsystem->SetEndpointSource(
            [this](const core::Function<void(foundation::net::NetworkManager&)>& visit)
            {
                ForEachInstance(
                    [&visit](GameInstance& gi)
                    {
                        if (foundation::net::NetworkManager* endpoint = gi.NetEndpoint())
                        {
                            visit(*endpoint);
                        }
                    });
            });
        m_audio = host.Ctx().AddSubsystem<engine::audio::AudioSubsystem>(m_audioEngineSettings);
        m_input = host.Ctx().AddSubsystem<engine::input::InputSubsystem>(
            host.Shell() != nullptr ? host.Shell()->Input() : nullptr);
        // The primary instance's per-instance input reads the shell devices by default (the player
        // path); the editor Game tab overrides this to its gated viewport source per tab.
        m_instance.SetInputSource(&m_input->ShellSource());
        m_ui = host.Ctx().AddSubsystem<engine::ui::UISubsystem>(host.Ctx().Allocator(),
                                                               *m_dataFileSystem);

        // Entity behaviors. Facade/backend registration is
        // batteries-included here (idempotent - entry points may register more);
        // the run context itself is created lazily by the subsystem and SHARED
        // with the game script (one gameplay context per run, the locked rule).
        m_scripts = host.Ctx().AddSubsystem<engine::script::ScriptSubsystem>();
        // The instance owns its run host; wire it with the app's facades
        // + Scene.spawn + entity.send routing so its context has them when the game script starts.
        // (The subsystem's own default host - for editor scenes - is wired in its OnReady.)
        m_scripts->ConfigureRunHost(m_instance.RunHost());
        InstallInstanceLoadFacade(m_instance, host); // run.* level-load facade for the primary instance
        // The COMPLETE facade surface via the composition root - never a hand-picked subset. A
        // hand-rolled list here once drifted past RegisterRunScriptFacade, so the exported player
        // compiled game scripts against a prelude with no `run` (week-2026-08-22). The root keeps
        // every host - editor, cook, MCP, exported player - on the same registered surface and
        // under the same kSubsystemFacadeNameCount tripwire.
        engine::RegisterAllScriptFacades();
        // Every built backend registers (batteries-included); a run resolves by the game script's
        // LANGUAGE - one gameplay context per run stays the locked rule. Each backend is independently
        // toggleable (OPTION_ENABLE_ANGELSCRIPT / _LUAU); all build on every platform, web included.
#ifdef OPTION_HAS_ANGELSCRIPT
        foundation::script::angelscript::RegisterAngelScriptBackend();
#endif
#ifdef OPTION_HAS_LUAU
        foundation::script::RegisterLuauScriptBackend();
#endif
        // Networking: the Net facade type is registered by the surface root above; each
        // GameInstance owns its OWN endpoint and goes online at RUNTIME via the facade
        // (Net.startServer/connect from the game's menu) - no app-owned socket. The primary
        // instance carries the online hook (the prefab net-spawn resolver) + the optional startup
        // preset below; extras get the hook in CreateInstance. The per-instance net binding is
        // installed by GameInstance itself.
        m_instance.Network().SetSpawnResolverFactory([this] { return MakeSpawnResolver(); });
        ApplyNetworkStartup(
            m_instance); // enter a preset server/client role at startup (None = offline)

        DefaultApplication* self = this;

        m_scripts->SetContextConfigurator(core::Function<void(foundation::script::IScriptContext&)>{
            [self](foundation::script::IScriptContext& context)
            {
                if (self->m_input != nullptr)
                {
                    self->m_input->ExposeToScript(context);
                }
                if (self->m_audio != nullptr)
                {
                    self->m_audio->ExposeToScript(context, self->Resources());
                }
                if (self->m_render != nullptr)
                {
                    self->m_render->ExposeToScript(context); // `DebugDraw.of(scene)` -> DebugScene
                }
                // `ui` -> the app-wide screen tier (finders + push/pop over the ScreenStack).
                engine::uiscript::InstallUiScreenScriptService(context, self->m_uiScreenBinding);
            }});

        // Back the `ui` facade with the live screen tier: point the binding at the UISubsystem's
        // screen root + ScreenStack, and supply a cooked-UIDocument instantiator (resolve by guid
        // from the run's resource manager - read live, as it may attach later in the editor - then
        // instantiate the markup into a view tree).
        if (m_ui != nullptr)
        {
            m_uiScreenBinding.stack = &m_ui->Screens();
            m_uiScreenBinding.instantiate =
                core::Function<core::RefPtr<foundation::ui::View>(const core::Guid&)>{
                    [self](const core::Guid& id) -> core::RefPtr<foundation::ui::View>
                    {
                        if (self->m_ui == nullptr || self->Resources() == nullptr || id.IsNil())
                        {
                            return {};
                        }
                        auto proxy = self->Resources()->Bind<foundation::ui::UIDocument>(id);
                        foundation::ui::UIDocument* document = proxy.Get();
                        return document != nullptr ? self->m_ui->InstantiateScreenOverlay(*document)
                                                   : core::RefPtr<foundation::ui::View>{};
                    }};
        }
        // Track A resource swaps (SceneRender.setMesh, ...): give the run a GETTER for the app's
        // resource manager (created later, in OnStartup), so a behavior can bind a resource id onto
        // a component's Ref. Borrowed - the app owns it.
        m_scripts->SetResourceManager(core::Function<foundation::resource::ResourceManager*()>{
            [self]() -> foundation::resource::ResourceManager* { return self->Resources(); }});

        // Composition-root bridge: forward physics contacts to the script subsystem's
        // neutral ingress. Keeps the two subsystems independent - neither depends on the
        // other for scripting; the wiring lives here, where integration belongs.
        if (m_physics != nullptr && m_scripts != nullptr)
        {
            m_contactBridge.Install(*m_physics, *m_scripts);
        }
    }

    engine::script::ScriptSubsystem* DefaultApplication::Scripts() const noexcept
    {
        return m_scripts;
    }

    foundation::scene::SceneManager& DefaultApplication::PrimaryScenes() noexcept
    {
        return m_instance.Scenes();
    }

    GameInstance* DefaultApplication::CreateInstance(bool headless)
    {
        if (m_scenes == nullptr || m_scripts == nullptr || m_host == nullptr)
        {
            return nullptr;
        }
        core::UniquePtr<GameInstance> owned =
            core::MakeUnique<GameInstance>(core::DefaultAllocator());
        GameInstance* gi = owned.Get();
        gi->SetHeadless(headless);
        m_scenes->RegisterManager(&gi->Scenes());
        m_scripts->ConfigureRunHost(gi->RunHost());
        InstallInstanceLoadFacade(*gi, *m_host); // run.* level-load facade for this extra instance
        gi->Network().SetSpawnResolverFactory( // its endpoints, wired like the primary
            [this] { return MakeSpawnResolver(); });
        if (m_input != nullptr)
        {
            gi->SetInputSource(&m_input->ShellSource());
        } // editor tabs override to their viewport
        m_extraInstances.PushBack(Move(owned));
        return gi;
    }

    void DefaultApplication::ReleaseInstance(GameInstance* instance)
    {
        if (instance == nullptr || instance == &m_instance)
        {
            return;
        }
        for (core::usize i = 0; i < m_extraInstances.Size(); ++i)
        {
            if (m_extraInstances[i].Get() != instance)
            {
                continue;
            }
            if (m_scenes != nullptr)
            {
                m_scenes->UnregisterManager(&instance->Scenes());
            }
            instance->Scenes().Clear(); // destroy any remaining scenes (aware subsystems notified)
            instance->RunHost().Teardown();
            m_extraInstances.RemoveAt(i); // frees the GameInstance
            return;
        }
    }

    void DefaultApplication::ApplyLoadedSceneActivation(foundation::scene::Scene* scene)
    {
        if (scene == nullptr)
        {
            return;
        }
        scene->Start();
        scene->SetSimulationEnabled(true);
    }

    void DefaultApplication::InstallInstanceLoadFacade(GameInstance& gi, IApplicationHost& host)
    {
        DefaultApplication* self = this;
        GameInstance* instance = &gi;

        // The render/sim policy PumpScriptLoads runs when a tracked load finishes (SetScene already
        // done by then). Virtual, so the player seeds a camera; the base just starts + simulates.
        gi.SetSceneActivationPolicy(core::Function<void(foundation::scene::Scene*)>{
            [self](foundation::scene::Scene* scene) { self->ApplyLoadedSceneActivation(scene); }});

        // run.loadSceneAsync(id) -> resolve the cooked scene instance, kick an async load into THIS
        // instance, register it under a ticket. 0 = could not start (bad id / no DB). The prefab
        // provider reads a nested-prefab payload by guid - the same source the sync path uses.
        gi.RunBinding().loadSceneAsync =
            core::Function<core::i32(const core::Guid&)>{[self, instance](const core::Guid& sceneId) -> core::i32
            {
                if (self->m_contentDatabase == nullptr || self->Resources() == nullptr)
                {
                    return 0;
                }
                foundation::content::Instance* sceneInst = self->m_contentDatabase->GetInstance(sceneId);
                if (sceneInst == nullptr)
                {
                    return 0;
                }
                foundation::content::IContentDatabase* db = self->m_contentDatabase;
                engine::runtime::SceneLoadHandle handle = instance->LoadSceneAsync(
                    *sceneInst, *self->Resources(),
                    core::Function<core::UniquePtr<core::IStream>(const core::Guid&)>{
                        [db](const core::Guid& prefabId) -> core::UniquePtr<core::IStream>
                        {
                            foundation::content::Instance* prefab = db->GetInstance(prefabId);
                            return (prefab != nullptr) ? prefab->ReadData(u8"scene")
                                                       : core::UniquePtr<core::IStream>{};
                        }});
                return instance->TrackScriptLoad(core::Move(handle));
            }};

        gi.RunBinding().loadProgress = core::Function<core::f64(core::i32)>{
            [instance](core::i32 ticket) -> core::f64
            { return static_cast<core::f64>(instance->ScriptLoadProgress(ticket)); }};
        gi.RunBinding().loadComplete = core::Function<bool(core::i32)>{
            [instance](core::i32 ticket) -> bool { return instance->ScriptLoadComplete(ticket); }};
        gi.RunBinding().loadFailed = core::Function<bool(core::i32)>{
            [instance](core::i32 ticket) -> bool { return instance->ScriptLoadFailed(ticket); }};

        // run.loadScene(id): synchronous convenience for tiny scenes - load, make current, apply the
        // same activation policy, all before the call returns. false on a resolve/load failure.
        gi.RunBinding().loadScene =
            core::Function<bool(const core::Guid&)>{[self, instance](const core::Guid& sceneId) -> bool
            {
                if (self->m_contentDatabase == nullptr || self->Resources() == nullptr)
                {
                    return false;
                }
                foundation::content::Instance* sceneInst = self->m_contentDatabase->GetInstance(sceneId);
                if (sceneInst == nullptr)
                {
                    return false;
                }
                foundation::content::IContentDatabase* db = self->m_contentDatabase;
                foundation::scene::Scene* scene = instance->LoadScene(
                    *sceneInst, *self->Resources(),
                    core::Function<core::UniquePtr<core::IStream>(const core::Guid&)>{
                        [db](const core::Guid& prefabId) -> core::UniquePtr<core::IStream>
                        {
                            foundation::content::Instance* prefab = db->GetInstance(prefabId);
                            return (prefab != nullptr) ? prefab->ReadData(u8"scene")
                                                       : core::UniquePtr<core::IStream>{};
                        }});
                if (scene == nullptr)
                {
                    return false;
                }
                instance->SetScene(scene); // current-scene bookkeeping (async path does this in Pump)
                self->ApplyLoadedSceneActivation(scene);
                return true;
            }};

        gi.RunBinding().sceneReady =
            core::Function<bool()>{[instance]() -> bool { return instance->SceneReady(); }};
        gi.RunBinding().currentScene = core::Function<scene::Scene*()>{
            [instance]() -> scene::Scene* { return instance->GetScene(); }};

        // run.requestExit(code): end the run through the app host. Standalone (ApplicationHost) stops
        // the loop; the editor's EmbeddedApplicationHost routes to the exit handler the editor set,
        // which stops the Game tab's play session. The host outlives the binding (app/session lifetime),
        // so a borrowed pointer is safe.
        gi.RunBinding().requestExit = core::Function<void(core::i32)>{
            [hostPtr = &host](core::i32 code) { hostPtr->RequestExit(code); }};

        // run.setTimeScale(scale): scale the run's GAMEPLAY time by setting the instance's scene-GROUP
        // time scale (SceneManager). 0 pauses the scene (behaviors + physics freeze); the Game script
        // tier keeps ticking so it can resume. Scoped to the run's scenes - the context/editor is
        // unaffected. Clamped to >= 0 (SceneManager also clamps).
        gi.RunBinding().setTimeScale = core::Function<void(core::f32)>{
            [instance](core::f32 scale) { instance->Scenes().SetTimeScale(scale < 0.0f ? 0.0f : scale); }};
        gi.RunBinding().timeScale =
            core::Function<core::f32()>{[instance]() -> core::f32 { return instance->Scenes().TimeScale(); }};
    }

    engine::physics::PhysicsSubsystem* DefaultApplication::Physics() const noexcept
    {
        return m_physics;
    }

    void
    DefaultApplication::SetAudioEngineSettings(const foundation::audio::AudioEngineSettings& settings)
    {
        m_audioEngineSettings = settings;
    }

    void
    DefaultApplication::SetResourceManager(foundation::resource::ResourceManager* borrowed) noexcept
    {
        m_borrowedResources = borrowed;
    }

    void
    DefaultApplication::SetContentDatabase(foundation::content::IContentDatabase* database) noexcept
    {
        m_contentDatabase = database;
        PointSpawnersAtContent();
    }

    void DefaultApplication::OnSystemsReady(foundation::scene::Scene& scene)
    {
        if (auto* spawner = scene.GetSystem<foundation::scene::PrefabSpawnSystem>())
        {
            spawner->SetSource(m_contentDatabase, Resources());
        }
    }

    void DefaultApplication::PointSpawnersAtContent()
    {
        ForEachInstance([this](GameInstance& gi)
                        { gi.Scenes().ForEachScene([this](foundation::scene::Scene& scene) { OnSystemsReady(scene); }); });
    }

    foundation::resource::ResourceManager* DefaultApplication::Resources() const noexcept
    {
        return m_borrowedResources != nullptr ? m_borrowedResources : m_ownedResources.Get();
    }

    void DefaultApplication::OnStartup(IApplicationHost& host)
    {
        // Product/runtime types: factories construct cooked products BY TYPE NAME.
        foundation::model::RegisterModelResourceTypes();
        foundation::image::RegisterImageResource();
        foundation::particles::RegisterParticleEffectResource();
        foundation::input::RegisterInputMapResource();
        foundation::physics::RegisterPhysicsResource();
        foundation::navigation::RegisterNavigationResource();
        foundation::audio::RegisterAudioResource();
        foundation::script::RegisterScriptResource();
        foundation::ui::RegisterUIResource();
        foundation::fonts::RegisterFontResource();
        core::GlobalTypeRegistry().Register(foundation::scene::SceneDocument::StaticType());
        core::GlobalTypeRegistry().Register(foundation::scene::PrefabDocument::StaticType());
        core::RegisterSerializable<foundation::scene::PrefabDocument>();
        core::RegisterSerializable<foundation::scene::SceneDocument>();
        engine::ui::RegisterUIComponentReflection();
        if (GraphicsDevice* gfx = host.Graphics();
            gfx != nullptr && gfx->Raw() != nullptr && m_ui != nullptr)
        {
            m_ui->EnsureRenderReady(*gfx->Raw(), gfx->FramesInFlight());
        }

        if (m_borrowedResources == nullptr && m_contentDatabase != nullptr)
        {
            // Share the global JobSystem so migrated factories can decode off the main thread
            // (async resource loading, task #123); null when there is no pool = synchronous loads.
            m_ownedResources = core::MakeUnique<foundation::resource::ResourceManager>(
                core::DefaultAllocator(), core::DefaultAllocator(), *m_contentDatabase,
                core::HasGlobalJobSystem() ? &core::GlobalJobs() : nullptr);
        }
        foundation::resource::ResourceManager* resources = Resources();
        PointSpawnersAtContent(); // the manager exists now; scenes composed earlier learn of it
        if (resources == nullptr)
        {
            return;
        } // headless/no-content apps (a project-manager editor attaches one later)
        RegisterStandardFactories(*resources, host);
    }

    void DefaultApplication::RegisterStandardFactories(foundation::resource::ResourceManager& resources,
                                                       IApplicationHost& host)
    {
        core::IAllocator& factoryAllocator = host.Ctx().Allocator();
        m_meshFactory =
            core::MakeUnique<foundation::geometry::StaticMeshFactory>(factoryAllocator,
                                                                      factoryAllocator);
        m_skinnedMeshFactory =
            core::MakeUnique<foundation::geometry::SkinnedMeshFactory>(factoryAllocator,
                                                                       factoryAllocator);
        m_skeletonFactory = core::MakeUnique<foundation::animation::SkeletonFactory>(
            factoryAllocator, factoryAllocator);
        m_animationClipFactory = core::MakeUnique<foundation::animation::AnimationClipFactory>(
            factoryAllocator, factoryAllocator);
        m_animationGraphFactory = core::MakeUnique<foundation::animation::AnimationGraphFactory>(
            factoryAllocator, factoryAllocator);
        resources.AddFactory(m_meshFactory.Get());
        resources.AddFactory(m_skinnedMeshFactory.Get());
        resources.AddFactory(&m_materialFactory);
        resources.AddFactory(m_skeletonFactory.Get());
        resources.AddFactory(m_animationClipFactory.Get());
        resources.AddFactory(m_animationGraphFactory.Get());
        resources.AddFactory(&m_propertyAnimationClipFactory);
        resources.AddFactory(&m_particleEffectFactory);
        resources.AddFactory(&m_inputMapFactory);
        resources.AddFactory(&m_collisionShapeFactory);
        // Navigation zone products allocate from the runtime's allocator authority.
        m_navigationZoneFactory = core::MakeUnique<foundation::navigation::NavigationZoneFactory>(
            host.Ctx().Allocator(), host.Ctx().Allocator());
        resources.AddFactory(m_navigationZoneFactory.Get());
        resources.AddFactory(&m_physicalMaterialFactory);
        m_audioClipFactory = core::MakeUnique<foundation::audio::AudioClipFactory>(
            host.Ctx().Allocator(), host.Ctx().Allocator());
        m_busLayoutFactory = core::MakeUnique<foundation::audio::AudioBusLayoutFactory>(
            host.Ctx().Allocator(), host.Ctx().Allocator());
        m_soundCueFactory = core::MakeUnique<foundation::audio::SoundCueFactory>(
            host.Ctx().Allocator(), host.Ctx().Allocator());
        resources.AddFactory(m_audioClipFactory.Get());
        resources.AddFactory(m_busLayoutFactory.Get());
        resources.AddFactory(m_soundCueFactory.Get());
        m_scriptClassFactory = core::MakeUnique<foundation::script::ScriptClassFactory>(
            factoryAllocator, factoryAllocator);
        resources.AddFactory(m_scriptClassFactory.Get());
        resources.AddFactory(&m_modelFactory);
        m_uiDocumentFactory = core::MakeUnique<foundation::ui::UIDocumentFactory>(
            factoryAllocator, factoryAllocator);
        m_uiThemeFactory = core::MakeUnique<foundation::ui::UIThemeFactory>(factoryAllocator,
                                                                            factoryAllocator);
        resources.AddFactory(m_uiDocumentFactory.Get());
        resources.AddFactory(m_uiThemeFactory.Get());
        m_fontFactory = core::MakeUnique<foundation::fonts::FontFactory>(
            host.Ctx().Allocator(), host.Ctx().Allocator());
        resources.AddFactory(m_fontFactory.Get());
        // Terrain: the CPU factories (grid / bundle / splat raster) so a cooked Terrain binds. The
        // GPU sub-resources (layer albedos) still resolve through the device-gated texture factory
        // below; the splatmap is CPU now (engine.terrain derives its GPU texture).
        m_heightfieldFactory = core::MakeUnique<foundation::heightfield::HeightfieldFactory>(
            factoryAllocator, factoryAllocator);
        m_terrainFactory = core::MakeUnique<foundation::terrain::TerrainFactory>(factoryAllocator,
                                                                                 factoryAllocator);
        m_splatmapFactory = core::MakeUnique<foundation::terrain::SplatWeightsFactory>(
            factoryAllocator, factoryAllocator);
        resources.AddFactory(m_heightfieldFactory.Get());
        resources.AddFactory(m_terrainFactory.Get());
        resources.AddFactory(m_splatmapFactory.Get());
        if (GraphicsDevice* gfx = host.Graphics(); gfx != nullptr && gfx->Raw() != nullptr)
        {
            if (!m_textureFactory)
            {
                m_textureFactory = core::MakeUnique<foundation::texture::TextureFactory>(
                    factoryAllocator, factoryAllocator, *gfx->Raw());
            }
            resources.AddFactory(m_textureFactory.Get());
        }
    }

    void DefaultApplication::AttachResourceManager(foundation::resource::ResourceManager* borrowed,
                                                   IApplicationHost& host)
    {
        m_borrowedResources = borrowed;
        if (borrowed != nullptr)
        {
            RegisterStandardFactories(*borrowed, host);
        }
    }

    void DefaultApplication::OnShutdown(IApplicationHost& host)
    {
        // Drop the net subsystem's endpoint source before teardown - the stored callback captures `this`,
        // so it must not outlive the app (the context tick has already stopped, so this is belt-and-braces).
        if (auto* netSubsystem = host.Ctx().GetSubsystem<engine::net::NetworkSubsystem>())
        {
            netSubsystem->SetEndpointSource({});
        }
        m_scenes->UnregisterObserver(this); // the observer is this app; scenes may still compose below
        // Destroy the run's scenes while the aware subsystems are still alive (they get
        // OnSceneDestroyed). The editor's GamePage already cleared them per Stop; this covers the
        // player + any leftover. Do it FIRST, before subsystem teardown, for EVERY instance.
        ForEachInstance(
            [](GameInstance& gi)
            {
                gi.StopNetworking();
                gi.Scenes().Clear();
                gi.RunHost().Teardown();
            });
        m_contactBridge.Uninstall();
        if (auto* gfx = host.Graphics(); gfx != nullptr && gfx->Raw() != nullptr)
        {
            m_screenshot.Release(*gfx->Raw());
        }
        m_ownedResources = nullptr; // release products while the device is alive
        m_textureFactory = nullptr;
    }

    void DefaultApplication::SetPrimaryScene(foundation::scene::Scene* scene) noexcept
    {
        m_instance.SetScene(scene); // also repoints the instance's replicated scene when online
    }

    foundation::scene::Scene* DefaultApplication::PrimaryScene() const noexcept
    {
        return m_instance.GetScene();
    }

    void DefaultApplication::SetGameScriptErrorHandler(
        foundation::script::IScriptErrorHandler* handler) noexcept
    {
        m_instance.SetScriptErrorHandler(handler);
    }

    bool DefaultApplication::StartGameScript(core::StringView source, core::StringView name)
    {
        return m_instance.StartScript(source, name);
    }

    void DefaultApplication::OnRenderWindow(IApplicationHost& host, FrameContext& frame)
    {
        auto* render = host.Ctx().GetSubsystem<engine::render::RenderSubsystem>();
        auto* scenes = host.Ctx().GetSubsystem<engine::scene::SceneSubsystem>();
        if (render == nullptr || !render->IsReady() || scenes == nullptr ||
            frame.encoder == nullptr || frame.backbufferView == nullptr || frame.window == nullptr)
        {
            frame.Clear(0.08f, 0.09f, 0.12f, 1.0f); // no renderer - present a clear
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
        // Render every NON-headless instance's scenes (a headless dedicated
        // server simulates but isn't drawn) + the default group's loose scenes. Clear comes from
        // the scene's camera.
        ForEachInstance(
            [&](GameInstance& gi)
            {
                if (gi.IsHeadless())
                {
                    return;
                }
                for (foundation::scene::Scene* scene : gi.Scenes().ActiveScenes())
                {
                    render->RenderScene(*scene, frame.backbufferView, colorFormat, frame.width,
                                        frame.height);
                }
            });
        render->EndRendering(); // scene-tier overlays (HUD/billboards) draw inside the compose

        // Window-space overlays (screen-tier UI, diagnostics, ...) composite over the
        // finished frame through the generic registry - the host names no source.
        render->RenderOverlays(*frame.encoder, frame.backbufferView, colorFormat, frame.width,
                               frame.height, frame.frameIndex);
        FinishFrame(host, frame);
    }

    void DefaultApplication::CaptureScreenshot(core::StringView path)
    {
        m_screenshot.Request(path);
    }

    void DefaultApplication::FinishFrame(IApplicationHost& host, FrameContext& frame)
    {
        ++m_renderedFrames;
        if (m_screenshotOptions.Requested() && !m_screenshotOptionFired &&
            m_screenshotOptions.Due(m_renderedFrames, m_runSeconds))
        {
            m_screenshotOptionFired = true;
            m_screenshot.Request(m_screenshotOptions.path.AsView());
            m_screenshotExitPending = m_screenshotOptions.exitAfter;
        }
        if (!m_screenshot.Armed())
        {
            return;
        }
        auto* gfx = host.Graphics();
        if (gfx == nullptr || gfx->Raw() == nullptr || frame.encoder == nullptr ||
            frame.window == nullptr)
        {
            return; // stays armed for a frame that has a backbuffer
        }
        const bool recorded =
            m_screenshot.Record(*gfx->Raw(), *frame.encoder, frame.backbuffer,
                                frame.window->Swap()->Format(), frame.width, frame.height);
        if (!recorded && m_screenshotExitPending)
        {
            host.RequestExit(1); // asked for a file that cannot be produced: say so by exit code
        }
    }

    foundation::scene::EntityHandle DefaultApplication::ResolveNetworkPrefab(
        foundation::content::IContentDatabase* database, foundation::resource::ResourceManager* resources,
        foundation::scene::Scene& scene, const core::Guid& prefabId)
    {
        return foundation::scene::PrefabSpawnSystem::SpawnInto(scene, database, resources, prefabId);
    }

    net::StateReplication::SpawnHandler DefaultApplication::MakeSpawnResolver()
    {
        DefaultApplication* self = this;
        // The resolver (content DB -> a live prefab); the NetworkController applies it to each
        // endpoint. The database and resources are read WHEN INVOKED, not when wired - the entry
        // point hands the content database over after construction.
        return net::StateReplication::SpawnHandler{
            [self](foundation::scene::Scene& scene, const core::Guid& prefabId,
                   net::NetworkId) -> foundation::scene::EntityHandle
            { return ResolveNetworkPrefab(self->m_contentDatabase, self->Resources(), scene, prefabId); }};
    }

    void DefaultApplication::ApplyNetworkStartup(GameInstance& instance)
    {
        switch (m_netStartup.role)
        {
        case net::NetworkRole::Server:
            (void)instance.StartServer(m_netStartup.listenPort, m_netStartup.dedicated);
            break;
        case net::NetworkRole::Client:
            (void)instance.Connect(m_netStartup.serverHost.AsView(), m_netStartup.serverPort);
            break;
        case net::NetworkRole::None:
        default:
            break;
        }
    }
}
