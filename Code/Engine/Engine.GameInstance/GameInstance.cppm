// Engine::GameInstance - the `engine.gameinstance` module.
//
// A single RUNNING GAME as a first-class object (docs/design/game-instance.md): its scene pairing,
// its script run context + `Game` object + error sink, and its instance time scale. The player owns
// ONE (N=1); the editor will own an Array<GameInstance> (multi-instance play-in-editor + an in-editor
// headless dedicated server for networking.md). This extracts the run bracket the player and the
// editor's Game tab hand-roll identically today.
//
// Phase 1: owns the SCRIPT run state + bracket + the instance time-scale term; the scene is still
// created by the caller and paired in via SetScene (scene ownership + the create/load/Start/Stop/
// destroy sequence move here in a later phase). BORROWS the ScriptSubsystem and the app's subsystems
// (never owns them).

module;
#include "Core/Prelude.h"
#include "Core/Reflection/Reflect.h" // the run facade (RTTI_OBJECT)

export module engine.gameinstance;

export import :networkcontroller; // the run's networking, composed off this god object (P1)

import foundation.core;
import foundation.scene;
import foundation.shell;
import foundation.scene.resource; // LoadScene / ResolveSceneResources / ResolveScenePrefabs
import foundation.content;        // content::Instance (the cooked scene record)
import foundation.resource;       // ResourceManager + AsyncBindScope (async level load, task #123)
import foundation.script;
import engine.script;
import foundation.script.facades; // RegisterExtraFacadeName (the run behavior-prelude hook)
import foundation.net.manager; // NetworkManager + INetworkController + NetScriptBinding
import foundation.input;       // ActionRuntime + IInputSourceProvider + InputMap (per-instance input)

using namespace foundation::core;
using namespace foundation;
namespace content = foundation::content;
namespace core = foundation::core;
namespace resource = foundation::resource;

export namespace engine::runtime
{
    // Foundation aliases (sibling engine::* namespaces would otherwise shadow these).
    namespace net = foundation::net;
    namespace scene = foundation::scene;
    namespace script = foundation::script;


    namespace scene = foundation::scene;
    namespace script = foundation::script;
    namespace net = foundation::net;
    namespace input = foundation::input;

    // EndpointOnlineHook now lives in the :networkcontroller partition (re-exported above).

    // ---- run.* script facade (task #123 + game-ready-scripting2 P2-2): the run/app tier surfaced to
    // scripts, including the running instance's LEVEL-LOAD control. Owned HERE (the project that owns
    // load orchestration), NOT the neutral Foundation facade lib - the out-of-tree pattern
    // foundation.net's Net facade uses. A RunScriptBinding is installed as a per-context service; the
    // app fills its pointers, backed by THIS instance's LoadSceneAsync + ticket registry + the content
    // DB (guid -> cooked scene). Bound to scripts LOWERCASE as `run` (NOT Game): a facade sharing the
    // mandatory `Game` orchestrator class name is a hard AngelScript name conflict.

    inline constexpr StringView kRunScriptService = u8"run.runtime";

    // Installed as a per-context script service; the run facade resolves it. The app fills the
    // pointers (backed by the owning GameInstance + content DB). Null pointers = safe no-ops.
    struct RunScriptBinding
    {
        core::Function<i32(const core::Guid&)> loadSceneAsync; // -> ticket (0 = failed to start)
        core::Function<f64(i32)> loadProgress;                 // ticket -> 0..1
        core::Function<bool(i32)> loadComplete;                // ticket -> complete?
        core::Function<bool(i32)> loadFailed;                  // ticket -> failed?
        core::Function<bool(const core::Guid&)> loadScene;     // sync load -> success
        core::Function<bool()> sceneReady;                     // current scene loaded + active?
        core::Function<scene::Scene*()> currentScene;          // the instance's live scene (or null)
        core::Function<void(i32)> requestExit;                 // end the run (standalone stops the loop;
                                                               // the editor stops the Game tab's session)
        // This run's event bus (game-ready-scripting2 P2-2): ONE service carries load + run-bus (Fable
        // ruling). run.events() publishes here. The GameInstance fills it with &RunEvents().
        scene::EventBus* runEvents = nullptr;
    };

    inline void InstallRunScriptService(script::IScriptContext& context, RunScriptBinding& binding)
    {
        context.SetService(kRunScriptService, &binding);
    }
    inline void ClearRunScriptService(script::IScriptContext& context)
    {
        context.SetService(kRunScriptService, nullptr);
    }

    // ---- run.* script facade (game-ready-scripting2 P2-2): the run/app tier surfaced to scripts. A
    // STATIC facade bound to scripts LOWERCASE as `run` (the ScriptName alias). run.events() -> the
    // run-bus handle (publish to the run tier); the load methods control this instance's level loads.
    // Nothing else may bind the name `run` (the FinalizeTypes collision trap enforces it).

    // A BOUND run-event-bus handle: `run.events().emit("Delivered", n)` publishes a NAMED event onto this
    // run's bus (deferred; delivered at the next run-bus drain). Mirrors SceneEvents; the payload is a
    // Variant (the one currency that crosses C++<->script and AngelScript<->Luau). Value type.
    struct RunEvents
    {
        scene::EventBus* bus = nullptr; // NOT reflected - resolved from the run context at run.events()
        void emit(core::String name) const;
        void emit(core::String name, core::Variant payload) const;
    };

    /// run.*: the run/app tier. `run.events()` is the run-bus handle; `loadSceneAsync` kicks an async
    /// scene load and returns a ticket the script polls - `var t = run.loadSceneAsync(id); while
    /// (!run.loadComplete(t)) yield`. loadScene is a sync convenience; sceneReady reports whether the
    /// instance's current scene is live. Static facade (resolve per call via CurrentScriptContext),
    /// bound to scripts as `run` via ScriptName. Resolves its OWN per-context service; unwired -> safe
    /// no-ops (an empty RunEvents whose emit no-ops; loadComplete returns true so a poll never hangs).
    class Run final : public Object
    {
        RTTI_OBJECT(Run, Object)
    public:
        [[nodiscard]] static RunScriptBinding* Resolve()
        {
            script::IScriptContext* context = script::CurrentScriptContext();
            return context != nullptr
                       ? static_cast<RunScriptBinding*>(context->GetService(kRunScriptService))
                       : nullptr;
        }
        [[nodiscard]] static RunEvents events()
        {
            RunScriptBinding* b = Resolve();
            RunEvents handle;
            handle.bus = (b != nullptr) ? b->runEvents : nullptr;
            return handle;
        }
        [[nodiscard]] static i32 loadSceneAsync(core::Guid scene)
        {
            RunScriptBinding* b = Resolve();
            return (b != nullptr && b->loadSceneAsync && !scene.IsNil()) ? b->loadSceneAsync(scene)
                                                                         : 0;
        }
        [[nodiscard]] static f64 loadProgress(i32 ticket)
        {
            RunScriptBinding* b = Resolve();
            return (b != nullptr && b->loadProgress) ? b->loadProgress(ticket) : 1.0;
        }
        [[nodiscard]] static bool loadComplete(i32 ticket)
        {
            RunScriptBinding* b = Resolve();
            return (b == nullptr || !b->loadComplete) ? true : b->loadComplete(ticket);
        }
        [[nodiscard]] static bool loadFailed(i32 ticket)
        {
            RunScriptBinding* b = Resolve();
            return (b != nullptr && b->loadFailed) ? b->loadFailed(ticket) : false;
        }
        [[nodiscard]] static bool loadScene(core::Guid scene)
        {
            RunScriptBinding* b = Resolve();
            return (b != nullptr && b->loadScene && !scene.IsNil()) ? b->loadScene(scene) : false;
        }
        [[nodiscard]] static bool sceneReady()
        {
            RunScriptBinding* b = Resolve();
            return (b != nullptr && b->sceneReady) ? b->sceneReady() : false;
        }
        /// The instance's current scene as a BOUND Scene handle - the orchestrator's door to
        /// `scene.spawn/find` (it has no entity to reach a scene through). Resolved AT THE CALL from
        /// instance state, so it is deterministic regardless of who invoked the script. Pre-scene /
        /// unwired -> a null-scene handle whose methods are safe no-ops.
        [[nodiscard]] static script::Scene currentScene()
        {
            RunScriptBinding* b = Resolve();
            script::Scene handle;
            handle.scene = (b != nullptr && b->currentScene) ? b->currentScene() : nullptr;
            return handle;
        }
        /// run.requestExit(code): end the run with an exit code (default 0). Routes to the app host -
        /// standalone stops the loop; the editor's embedded host stops the Game tab's play session.
        /// Unwired -> a safe no-op (the standard guard).
        static void requestExit(i32 code)
        {
            RunScriptBinding* b = Resolve();
            if (b != nullptr && b->requestExit)
            {
                b->requestExit(code);
            }
        }
        /// Arity-0 convenience: exit with code 0 (mirrors the C++ host default). Exposed as its own
        /// overload so `run::requestExit()` / `run.requestExit()` bind on both backends.
        static void requestExit() { requestExit(0); }
    };

    // Registers the `Run` facade (bound to scripts as `run` via ScriptName) + its RunEvents handle type
    // + the behavior-prelude name. Idempotent; call before a script manager is created (the run host /
    // cook builder do).
    void RegisterRunScriptFacade();

    // Handle for an in-flight ASYNC scene load (task #123). LoadSceneAsync creates the scene
    // INACTIVE (not ticked/rendered) and kicks its resources off on workers; poll IsComplete() /
    // Progress() to drive a loading screen, then GameInstance::ActivateLoadedScene() to bring it
    // live. Scene() is the pending scene - its RESOURCES are ready only once IsComplete(). A tiny
    // value type (mirrors resource::AsyncLoadBatch + the scene/failed state); safe to copy/store.
    //
    // LIMITATION (Fable review): IsComplete/Progress poll ResourceManager::PendingCount - the WHOLE
    // manager - and m_total snapshots that global count. Two overlapping loads (or any unrelated
    // BindAsync in flight) conflate: each handle waits on ALL pending work (the SAFE direction) and
    // progress distorts. Fine for the boot + single-level-switch reality here; MUST move to per-batch
    // tracking (resource::AsyncLoadBatch already does exactly this) before overlapping / background
    // loads become real.
    class SceneLoadHandle
    {
    public:
        SceneLoadHandle() = default;

        [[nodiscard]] bool Failed() const noexcept { return m_failed; }
        [[nodiscard]] scene::Scene* Scene() const noexcept { return m_scene; }
        [[nodiscard]] bool IsComplete() const noexcept
        {
            return m_failed || m_resources == nullptr || m_resources->PendingCount() == 0;
        }
        // 0..1; 1 when complete or failed.
        [[nodiscard]] core::f32 Progress() const noexcept
        {
            if (m_failed || m_resources == nullptr || m_total == 0)
            {
                return 1.0f;
            }
            const core::usize remaining = m_resources->PendingCount();
            if (remaining == 0)
            {
                return 1.0f;
            }
            const core::usize done = (remaining >= m_total) ? 0u : (m_total - remaining);
            return static_cast<core::f32>(done) / static_cast<core::f32>(m_total);
        }

    private:
        friend class GameInstance;
        scene::Scene* m_scene = nullptr;
        resource::ResourceManager* m_resources = nullptr;
        core::usize m_total = 0;
        bool m_failed = false;
    };

    // A running game COMPOSES its networking: GameInstance holds a NetworkController (which IS the
    // INetworkController the Net facade drives) and forwards to it (networking-extraction.md P1). The
    // controller is a stable member, so its binding never dangles; the endpoint inside it comes and goes.
    class GameInstance final
    {
    public:
        void SetScene(scene::Scene* scene) noexcept
        {
            m_scene = scene;
            m_network.SetReplicatedScene(scene); // keep replication on the current scene across loads
        }
        [[nodiscard]] scene::Scene* GetScene() const noexcept { return m_scene; }

        void SetScriptErrorHandler(script::IScriptErrorHandler* handler) noexcept
        {
            m_errorHandler = handler;
        }
        [[nodiscard]] script::IScriptErrorHandler* ScriptErrorHandler() const noexcept
        {
            return m_errorHandler;
        }

        /// Headless: simulate + run scripts, but the host does NOT render this instance (no camera/
        /// swapchain needed) - the in-editor dedicated server (game-instance.md §11 / networking.md).
        void SetHeadless(bool headless) noexcept { m_headless = headless; }
        [[nodiscard]] bool IsHeadless() const noexcept { return m_headless; }

        /// This run's global time scale - the `instance` term in the generalized time model
        /// (dt a scene sees = host dt x context scale x INSTANCE scale x scene scale). Defaults to 1,
        /// so at N=1 it collapses to the previous two-level model (no behaviour change).
        void SetInstanceTimeScale(f32 scale) noexcept { m_instanceTimeScale = scale; }
        [[nodiscard]] f32 InstanceTimeScale() const noexcept { return m_instanceTimeScale; }

        /// Compile + launch the `Game` script (a class with launch()/update(dt)/exit()) on THIS instance's
        /// run host (game-instance.md §11.10). The host must be configured first (the app's
        /// ScriptSubsystem::ConfigureRunHost exposes the facades + routing); a bare test just needs a
        /// backend registered. Idempotent start (stops a prior run first). false on compile / no-`Game`.
        bool StartScript(core::StringView source, core::StringView name)
        {
            return StartScript(source, name, core::Span<const core::String>{});
        }

        /// As above, plus the Game class's declared handler names (from the cooked ScriptClass) so the
        /// Game tier's on<Event> inbox can subscribe to the run bus (game-ready-scripting2 §1a). A caller
        /// with no handler list (raw-source tests) gets a Game with no inbox - the rest is unchanged.
        bool StartScript(core::StringView source, core::StringView name,
                         core::Span<const core::String> gameHandlers);

        /// exit() the `Game` + release the game-script hold (idempotent; the update-fault path lands here).
        /// The run host tears down when nothing else pins it (the scene-stop observer drives that).
        void StopScript();

        /// Create a scene in this instance's group AND bind its behaviors to this instance's run host
        /// (game-instance.md §11.10). Use this instead of Scenes().CreateScene so the re-bind happens.
        /// `activate` (default true) matches the classic behavior; false creates it inactive for an
        /// async load (see LoadSceneAsync).
        scene::Scene* CreateScene(core::StringView name, bool activate = true);
        /// Destroy a scene in this instance's group. Drops any tracked async load whose pending
        /// target IS this scene BEFORE freeing it, so PumpScriptLoads can never activate a dangling
        /// Scene* (Fable review finding: the handle holds a raw Scene*). A dropped ticket then reads
        /// terminal-safe (complete=true, failed=false) via the unknown-ticket fallback.
        void DestroyScene(scene::Scene* scene)
        {
            for (core::usize i = m_scriptLoads.Size(); i > 0; --i)
            {
                if (m_scriptLoads[i - 1].handle.Scene() == scene)
                {
                    m_scriptLoads.RemoveAt(i - 1);
                }
            }
            // Scene-dies-before-endpoint (networking-extraction.md P2, Fable req 2): if this IS the
            // replicated scene, clear the cache + detach its NetworkSceneSystem BEFORE it is freed, so a
            // later Start/Connect never wires a dead scene and the live endpoint stops replicating it.
            if (scene == m_scene)
            {
                SetScene(nullptr);
            }
            m_sceneManager.DestroyScene(scene);
        }

        /// Tear down ALL of this instance's scenes for a run STOP that KEEPS the instance alive (the
        /// editor Game tab stopping between plays). Drops every in-flight tracked script load FIRST -
        /// the handles hold a raw Scene* into the group, so a later PumpScriptLoads must not activate a
        /// just-freed scene - then clears the current-scene pointer (net replication follows) and
        /// destroys the whole group (aware subsystems notified). ReleaseInstance/OnShutdown free the
        /// entire instance, so they only need Scenes().Clear(); this is for the persistent-instance stop.
        void ClearScenes()
        {
            m_scriptLoads.Clear();
            SetScene(nullptr);
            m_sceneManager.Clear();
        }

        // ---- scene / level load (task #123): the load ORCHESTRATION lives on the instance (the
        // user-controllable unit), de-duping the identical block PlayerApplication + GamePageImpl
        // both ran. The APP still owns POLICY (WHAT to load; EnsureCamera + Start + SetScene after).

        /// Load a cooked scene instance into this group and resolve its resources SYNCHRONOUSLY.
        /// Returns the loaded, ACTIVE, fully-resolved scene (NOT started - the caller runs its policy:
        /// EnsureCamera, Start, SetSimulationEnabled, SetScene). Null on a LoadScene failure.
        /// `prefabProvider` reads a nested-prefab payload by guid (empty function = no prefabs).
        scene::Scene*
        LoadScene(content::Instance& sceneInstance, resource::ResourceManager& resources,
                  core::Function<core::UniquePtr<core::IStream>(const core::Guid&)> prefabProvider);

        /// Async load: creates the scene INACTIVE, deserializes it, and kicks its resource binds onto
        /// workers (BindAsync). Returns a SceneLoadHandle - poll IsComplete()/Progress() (loading
        /// screen), then ActivateLoadedScene(). The scene never ticks/renders while loading.
        [[nodiscard]] SceneLoadHandle LoadSceneAsync(
            content::Instance& sceneInstance, resource::ResourceManager& resources,
            core::Function<core::UniquePtr<core::IStream>(const core::Guid&)> prefabProvider);

        /// Activate a COMPLETED async-loaded scene (add to the active/render set + make current).
        /// Returns the now-active scene, or null if the handle failed or is not yet complete. The
        /// caller then runs the same policy as the sync path (EnsureCamera, Start, ...).
        scene::Scene* ActivateLoadedScene(SceneLoadHandle& handle);

        // ---- script-driven scene loads (task #123): the run facade's `loadSceneAsync -> ticket ->
        // poll` model. The instance owns the in-flight handles keyed by ticket; the app drives
        // completion each frame via PumpScriptLoads, which activates a finished load, makes it the
        // current scene (SetScene), then runs the app's render/sim POLICY hook. Kept on the instance
        // (not the app) so the orchestrator script that launched with NO scene resolves its loads
        // through its OWN instance host - see the app's per-host facade install.

        /// The post-activation policy the APP owns (EnsureCamera, Start, SetSimulationEnabled - the
        /// render/sim half; SetScene is the instance's own bookkeeping and runs first). Set once by
        /// the app; PumpScriptLoads invokes it on a scene the moment its tracked load completes.
        using SceneActivationPolicy = core::Function<void(scene::Scene*)>;
        void SetSceneActivationPolicy(SceneActivationPolicy policy)
        {
            m_activatePolicy = core::Move(policy);
        }

        /// Register an in-flight async load under a fresh ticket (1-based; 0 is never issued, so it
        /// doubles as "failed to start"). The run facade hands this ticket to the script;
        /// ScriptLoad{Progress,Complete,Failed} query by it. The handle is a value type (stored).
        ///
        /// CAUTION (Fable review): the handle holds a raw Scene*. Nothing destroys a PENDING inactive
        /// scene today, so PumpScriptLoads never activates a dangling pointer. If a path that destroys
        /// a pending scene ever appears (instance-level scene sweeps, a script-driven load cancel),
        /// it MUST clear the matching tracked load(s) first, or PumpScriptLoads would activate freed
        /// memory.
        i32 TrackScriptLoad(SceneLoadHandle handle);

        /// Drive tracked script loads: any whose resources finished get ActivateLoadedScene +
        /// SetScene + the activation policy, exactly once. Call each frame (the app's OnUpdate,
        /// after the resource pump). Cheap when nothing is in flight.
        void PumpScriptLoads();

        /// Ticket queries for the run facade. Unknown ticket -> Progress 1, Complete true (a bad or
        /// expired ticket never hangs a `while (!complete) yield` loop), Failed false.
        [[nodiscard]] f32 ScriptLoadProgress(i32 ticket) const;
        [[nodiscard]] bool ScriptLoadComplete(i32 ticket) const; // true once terminal (live OR failed)
        [[nodiscard]] bool ScriptLoadFailed(i32 ticket) const;

        /// run.sceneReady: this instance has a live current scene. A script on the app-driven
        /// boot path waits `while (!run.sceneReady()) yield` instead of assuming a scene at
        /// launch().
        [[nodiscard]] bool SceneReady() const noexcept { return m_scene != nullptr; }

        /// The run facade's per-context binding for THIS instance - the app fills its pointers
        /// (backed by LoadSceneAsync + the ticket registry + the content DB); GameInstance installs it
        /// on the run context when the game script starts. Stable member, so it never dangles.
        [[nodiscard]] RunScriptBinding& RunBinding() noexcept
        {
            return m_runBinding;
        }

        /// Tick the `Game` script with gameplay time: hostDt x contextScale x instanceScale x sceneScale.
        /// A faulting update disables THIS instance's script (drops the `Game`), not the app.
        void TickScript(f32 hostDeltaTime, f32 contextTimeScale);

        /// Drive this instance's run host each frame: advance the script binding clock (Time.now/delta)
        /// and step its GC. The subsystem drives its OWN (editor) host; each instance drives its own.
        void DriveRunHost(f32 deltaTime);

        [[nodiscard]] bool ScriptRunning() const noexcept { return m_game.Get() != nullptr; }
        [[nodiscard]] script::IScriptContext* ScriptContext() const noexcept
        {
            return m_scriptContext.Get();
        }

        /// This run's script host - the gameplay context shared by the game script AND this instance's
        /// scenes' behaviors ("one gameplay context per instance", game-instance.md §11). Owned HERE now;
        /// the ScriptSubsystem borrows it (a later step has the instance drive it directly). Moving the
        /// storage onto the instance is the prerequisite for per-instance runs (Array<GameInstance>).
        [[nodiscard]] engine::script::ScriptRunHost& RunHost() noexcept { return m_runHost; }

        /// This run's scene group (game-instance.md §11.2 / §4.4): the set of scenes the run manages + its
        /// current scene, ticked on the Context lane once registered with the SceneSubsystem. Wire its
        /// aware-registry (from the SceneSubsystem) before creating scenes in it.
        [[nodiscard]] scene::SceneManager& Scenes() noexcept { return m_sceneManager; }

        /// This run's RUN-SCOPED event bus (game-ready-scripting2 §1a): app-owned, ONE per run. The Game
        /// tier's on<Event> inbox harvests it, and cross-scene / game-wide coordination publishes here
        /// (a Level re-emits what the run tier should hear - there is NO implicit scene->run relay). It is
        /// the SAME native scene::EventBus type (scene-agnostic: StringHash + Variant + subscriber list).
        [[nodiscard]] scene::EventBus& RunEvents() noexcept { return m_runEvents; }

        /// Deliver this frame's queued run-bus events. Called on the instance tick AFTER TickScript (no VM
        /// call active), so a subscriber may dispatch script handlers directly. Cascade-bounded like the
        /// scene bus; safe with no game script (native run-bus subscribers still fire).
        void DrainRunEvents() { m_runEvents.Drain(); }

        // ---- input (this instance's OWN action runtime; game-instance.md - the input analog of the
        // per-instance scene group + net endpoint) ----

        /// This run's input source (the editor Game tab's gated viewport, or the player's shell devices).
        /// The host sets it; the per-instance runtime reads ONLY this source, so per-surface focus gating
        /// isolates input across Game tabs (only the focused tab's source reports keys). Null = no input.
        void SetInputSource(input::IInputSourceProvider* source) noexcept
        {
            m_inputSource = source;
        }

        /// Install the action map (the game's controls, from the project's input-map asset) on this
        /// instance's runtime. Each instance has its own runtime + map copy.
        void SetInputMap(const input::InputMap& map) { m_inputRuntime.SetMap(map); }

        /// This run's action runtime - the Input facade resolves it per script context (installed into
        /// this instance's context, so instance A's script never sees instance B's keys).
        [[nodiscard]] input::ActionRuntime& InputRuntime() noexcept { return m_inputRuntime; }

        /// Evaluate this instance's action runtime against its source (call before TickScript so the game
        /// sees this frame's input). No-op when no source is set.
        void DriveInput(f32 deltaTime, f32 contextTimeScale);

        // ---- networking (composed: this instance's NetworkController; forwards below) ----

        /// This instance's networking controller (the endpoint owner + INetworkController). Exposed so
        /// the app can reach it directly as the extraction proceeds; GameInstance's own net methods below
        /// are thin forwards preserved so existing callers/tests are churn-free (P1).
        [[nodiscard]] NetworkController& Network() noexcept { return m_network; }

        /// A hook the app sets once; the controller fires it (with the live endpoint) each time the game
        /// goes online, so the app can wire per-endpoint setup that needs app state (the net-spawn
        /// resolver from the content DB). Not consumed - reconnect re-runs it.
        void SetEndpointOnlineHook(EndpointOnlineHook hook) { m_network.SetEndpointOnlineHook(core::Move(hook)); }

        /// Drive this instance's networking on the FIXED lane (deterministic step). No-op when offline.
        /// Called by the app's fixed-step fan-out (moves to a subsystem in P2/P3).
        void DriveNetwork(f32 fixedDeltaMs) { m_network.DriveNetwork(fixedDeltaMs); }

        // Net facade role controls - forwards to the composed controller. StartServer/Connect open a
        // real UDP socket and enter the role (false if it fails); StopNetworking drops the endpoint.
        bool StartServer(u16 port, bool dedicated) { return m_network.StartServer(port, dedicated); }
        bool Connect(core::StringView host, u16 port) { return m_network.Connect(host, port); }
        void StopNetworking() { m_network.StopNetworking(); }
        [[nodiscard]] net::NetworkManager* NetEndpoint() const noexcept { return m_network.NetEndpoint(); }

    private:
        /// The run-bus sink for the Game tier: dispatch `on<Event>(payload)` to the Game object. Called at
        /// run-bus drain time (no VM active), so it invokes directly; a faulting handler disables the game
        /// (m_game = nullptr) exactly like a faulting update(), and a debugger suspension is not a fault.
        void DispatchGameEvent(core::StringView eventName, const core::Variant& payload);

        engine::script::ScriptRunHost m_runHost;    // owned: the game's script context (§11.10)
        scene::EventBus m_runEvents; // the run-scoped event bus (game-ready-scripting2 §1a; app-owned)
        scene::SceneManager m_sceneManager; // owned; registered with the SceneSubsystem to tick
        scene::Scene* m_scene = nullptr;
        script::IScriptErrorHandler* m_errorHandler = nullptr;
        bool m_headless = false;
        f32 m_instanceTimeScale = 1.0f;
        core::RefPtr<script::IScriptContext>
            m_scriptContext; // the game script's ref to the run host's context
        core::RefPtr<script::ScriptObject> m_game;
        engine::script::ScriptEventSubscriptions m_gameEventSubs; // Game tier's run-bus on<Event> inbox

        NetworkController m_network; // this run's networking (endpoint + INetworkController), composed (P1)
        RunScriptBinding
            m_runBinding; // stable; app fills its pointers, installed per context (StartScript)

        input::ActionRuntime m_inputRuntime; // this run's action state (per-instance)
        input::IInputSourceProvider* m_inputSource =
            nullptr; // borrowed: the viewport / shell devices

        // Script-driven scene loads (task #123): in-flight handles keyed by ticket. An entry lives
        // only while its load is IN FLIGHT or has FAILED - PumpScriptLoads RETIRES an entry the moment
        // it activates successfully (Fable review: was monotonic growth). A retired ticket reads
        // terminal-safe via the unknown-ticket fallback (progress 1, complete true, failed false),
        // which is exactly the post-activation answer, so dropping it is transparent to the script.
        // Failed entries linger so ScriptLoadFailed stays truthful; failures are exceptional, so the
        // array stays bounded across level switches. m_nextScriptTicket only grows -> tickets never alias.
        struct TrackedScriptLoad
        {
            i32 ticket = 0;
            SceneLoadHandle handle;
        };
        core::Array<TrackedScriptLoad> m_scriptLoads;
        i32 m_nextScriptTicket = 0;
        SceneActivationPolicy m_activatePolicy; // app-set render/sim policy, run on completion
    };

}
