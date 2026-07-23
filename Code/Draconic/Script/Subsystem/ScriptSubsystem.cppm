// Draconic::ScriptSubsystem - the `draconic.script.subsystem` module.
//
// Entity behaviors (docs/design/scripting.md §3 + §7 P1): a ScriptSceneSystem per scene
// instantiates each behavior's cooked ScriptClass in the run's ONE gameplay script
// context, applies harvested defaults + hash-keyed overrides, and dispatches the
// declared lifecycle handlers (onStart via deferred start, onUpdate(dt), onEnable/
// onDisable, onDestroy) - gated to simulation (IsSimulationOnly: the editor's Simulate
// toggle and PIE both count). A faulting BEHAVIOR is disabled and logged; scripting
// keeps running.
//
// Context ownership (the locked PIE rule): the subsystem's ScriptRunHost owns the run's
// single IScriptContext - created lazily at first use (first behavior instantiate, or
// the game script's StartGameScript, which SHARES it), torn down when the run ends
// (game script released + no live instances + nothing simulating). The backend is
// resolved through the ScriptBackendRegistry by LANGUAGE (B3) - no backend type is
// ever named here.
//
// Hot reload: the cook swaps the product inside the resource handle; the dispatch loop
// notices the behavior's bound product pointer changed, re-instantiates, and re-applies
// the hash-keyed overrides (editor-set values survive; transient script state resets -
// the documented v1 contract).

module;
#include "Core/Prelude.h"
#include "Core/Reflection/Reflect.h"
#include "Core/Log/Log.h"
#include "Profiler/Profiler.h"

export module draconic.script.subsystem;

export import :components;
export import draconic.script.facades; // Entity/Log/Time/Random + the run-service binding

import draconic.core;
import draconic.runtime;
import draconic.scene;
import draconic.scene.subsystem;
import draconic.resource;
import draconic.script;
import draconic.script.resource;
import draconic.profiler;

using namespace draconic::core;

export namespace draconic::script
{
    namespace scene = draconic::scene;

    // ---- the run's script host (ONE gameplay context per run) ----

    // Logs behavior errors (Script category) and forwards to an optional external sink
    // (the editor's console/notice path).
    class RunScriptErrorSink final : public IScriptErrorHandler
    {
    public:
        IScriptErrorHandler* external = nullptr;

        void OnError(const ScriptError& error) override
        {
            if (error.kind == ScriptErrorKind::Compile)
            {
                DRACONIC_LOG_ERROR(u8"Script", u8"{}:{}: {}", error.module, error.line,
                                   error.message);
            }
            else
            {
                DRACONIC_LOG_ERROR(u8"Script", u8"{} (line {}): {}", error.module, error.line,
                                   error.message);
            }
            if (external != nullptr)
            {
                external->OnError(error);
            }
        }
    };

    /// Tracks the debugger's pause state for the run (the game-pause flag the scene tick
    /// checks) and forwards state changes to an optional external listener (the editor's
    /// debugger panels). Owned by the run host so the flag survives across the whole run.
    class DebugPauseTracker final : public IScriptDebuggerListener
    {
    public:
        IScriptDebuggerListener* external = nullptr;
        [[nodiscard]] bool Paused() const noexcept { return m_paused; }
        void Reset() noexcept { m_paused = false; }
        void OnDebuggerStateChanged(ScriptDebuggerState state) override
        {
            m_paused =
                (state == ScriptDebuggerState::Breakpoint || state == ScriptDebuggerState::Stepped);
            if (external != nullptr)
            {
                external->OnDebuggerStateChanged(state);
            }
        }

    private:
        bool m_paused = false;
    };

    /// Owns the run's manager + context and the behaviors MODULE loaded into it. Class
    /// sources are concatenated into one generation-versioned module ("behaviors#N"):
    /// the contract's CreateInstance resolves against the LAST loaded module, and Wren
    /// forbids redefining a module variable - a fresh module name per generation gives
    /// hot reload clean semantics (live instances of old generations keep running).
    /// Plain class (no runtime deps) so headless tests drive it directly.
    class ScriptRunHost
    {
    public:
        /// Extra per-context wiring (the host app exposes Input/Audio/Physics services).
        void SetContextConfigurator(Function<void(IScriptContext&)> configurator)
        {
            m_configurator = Move(configurator);
        }
        void SetExternalErrorSink(IScriptErrorHandler* sink) noexcept
        {
            m_errorSink.external = sink;
        }

        // The game-script hold (game-instance.md §11.10): true while a game script is loaded into THIS
        // host, so teardown accounting keeps the context alive. Per-host now (was ScriptSubsystem-wide).
        void SetGameScriptHold(bool held) noexcept { m_gameScriptHold = held; }
        [[nodiscard]] bool HasGameScriptHold() const noexcept { return m_gameScriptHold; }

        // ---- step debugging (the PIE debugger + the future remote transport) ----

        /// Ask that this run be debuggable: a debugger is created on the run's manager
        /// (lazily, when the context exists) and `configurator` is called once with it so the
        /// caller can apply breakpoints + capture the pointer. The internal pause tracker is
        /// always the debugger's listener; a caller forwards through SetExternalDebugListener.
        void RequestDebugger(Function<void(IScriptDebugger&)> configurator)
        {
            m_debuggerConfigurator = Move(configurator);
            m_debuggerRequested = true;
            EnsureDebugger();
        }
        /// The run's debugger, or null (not requested / backend lacks the capability / no
        /// context yet). Contract-typed so a caller never depends on a backend.
        [[nodiscard]] IScriptDebugger* Debugger() const noexcept { return m_debugger.Get(); }
        /// Forward debugger state changes to an external listener (the editor's panels), on
        /// top of the internal pause tracking. Cleared on teardown.
        void SetExternalDebugListener(IScriptDebuggerListener* listener) noexcept
        {
            m_debugTracker.external = listener;
        }
        /// The game-pause flag: true while suspended at a breakpoint/step. The scene tick
        /// checks this to hold the world still; InvokeHandler checks it to tell a
        /// debug-suspended handler apart from a fault.
        [[nodiscard]] bool IsDebugPaused() const noexcept { return m_debugTracker.Paused(); }

        [[nodiscard]] IScriptContext* Context() const noexcept { return m_context.Get(); }
        [[nodiscard]] IScriptManager* Manager() const noexcept { return m_manager.Get(); }
        [[nodiscard]] StringView Language() const noexcept { return m_language.AsView(); }
        [[nodiscard]] ScriptRuntimeBinding& Binding() noexcept { return m_binding; }

        /// The run context for `languageId`, created through the backend REGISTRY on
        /// first use (B3). A second language during the same run is refused (one
        /// gameplay context per run - the locked rule) and logged once.
        [[nodiscard]] IScriptContext* EnsureContext(StringView languageId)
        {
            if (m_context.Get() != nullptr)
            {
                if (m_language.AsView() == languageId)
                {
                    return m_context.Get();
                }
                WarnLanguageMismatchOnce(languageId);
                return nullptr;
            }
            // The full curated surface: core math + the behavior facades, so a backend's
            // behavior-module framing and any explicit engine-type imports a script writes
            // resolve identically at cook and at runtime. Both idempotent.
            RegisterCoreTypes();
            RegisterScriptFacadeReflection();
            m_manager = CreateScriptManagerForLanguage(languageId);
            if (m_manager.Get() == nullptr)
            {
                DRACONIC_LOG_ERROR(u8"Script", u8"no script backend for language '{}'", languageId);
                return nullptr;
            }
            RegisterReflectedTypes(*m_manager);
            m_context = m_manager->CreateContext();
            if (m_context.Get() == nullptr)
            {
                m_manager = nullptr;
                return nullptr;
            }
            m_language = String(languageId);
            m_binding.timeSeconds = 0.0;
            m_binding.deltaSeconds = 0.0f;
            m_binding.random = core::Random{}; // fresh per-run RNG
            m_context->SetErrorHandler(&m_errorSink);
            m_context->SetService(kScriptRuntimeService, &m_binding);
            if (m_configurator)
            {
                m_configurator(*m_context);
            }
            m_moduleCurrent = false;
            EnsureDebugger(); // a debugger requested before the context existed attaches now
            DRACONIC_LOG_DEBUG(u8"Script", u8"run script context created ({})", languageId);
            return m_context.Get();
        }

        /// The run context resolved by a script FILE's extension (the game-script path).
        [[nodiscard]] IScriptContext* EnsureContextForFile(StringView path)
        {
            StringView extension;
            for (usize i = path.Size(); i-- > 0;)
            {
                if (path[i] == u8'.')
                {
                    extension = path.SubStr(i + 1, path.Size() - (i + 1));
                    break;
                }
                if (path[i] == u8'/' || path[i] == u8'\\')
                {
                    break;
                }
            }
            const ScriptBackendRegistry& registry = ScriptBackendRegistry::Get();
            const ScriptBackendDesc* backend =
                extension.IsEmpty() ? nullptr : registry.FindByExtension(extension);
            if (backend == nullptr && registry.All().Size() == 1)
            {
                backend = &registry.All()[0];
            }
            if (backend == nullptr)
            {
                DRACONIC_LOG_ERROR(u8"Script", u8"no script backend matches '{}'", path);
                return nullptr;
            }
            return EnsureContext(backend->languageId.AsView());
        }

        /// The GAME SCRIPT (or any external caller) loaded its own module: the behaviors
        /// module is no longer the context's instantiation target.
        void NoteExternalLoad() noexcept { m_moduleCurrent = false; }

        /// Instantiate a behavior class instance (loads/reloads the behaviors module as
        /// needed). Null on failure (logged).
        [[nodiscard]] RefPtr<ScriptObject> Instantiate(ScriptClass& scriptClass, Span<Variant> args)
        {
            if (!EnsureClassLoaded(scriptClass))
            {
                return nullptr;
            }
            RefPtr<ScriptObject> instance =
                m_context->CreateInstance(scriptClass.className.AsView(), args);
            if (instance.Get() == nullptr)
            {
                DRACONIC_LOG_ERROR(u8"Script", u8"class '{}' failed to instantiate",
                                   scriptClass.className);
            }
            return instance;
        }

        /// Compiles the class (with every other known behavior class) into the current
        /// behaviors module generation. False on compile failure or language mismatch.
        [[nodiscard]] bool EnsureClassLoaded(ScriptClass& scriptClass)
        {
            const StringView language = scriptClass.language.IsEmpty()
                                            ? StringView(u8"wren")
                                            : scriptClass.language.AsView();
            if (EnsureContext(language) == nullptr)
            {
                return false;
            }

            bool present = false;
            for (usize i = 0; i < m_loadedClasses.Size(); ++i)
            {
                ScriptClass* loaded = m_loadedClasses[i].Get();
                if (loaded == &scriptClass)
                {
                    present = true;
                    break;
                }
                // A reloaded product replaces its predecessor (same class name).
                if (loaded->className.AsView() == scriptClass.className.AsView())
                {
                    m_loadedClasses[i] = RefPtr<ScriptClass>(&scriptClass);
                    m_moduleCurrent = false;
                    present = true;
                    break;
                }
            }
            if (!present)
            {
                m_loadedClasses.PushBack(RefPtr<ScriptClass>(&scriptClass));
                m_moduleCurrent = false;
            }
            if (m_moduleCurrent)
            {
                return true;
            }

            // Rebuild: fresh generation module, each class carrying its OWN section identity
            // (sourceName). The structured LoadBehaviorModule preserves per-class sections so a
            // debug-capable backend reports (sourceFile, sourceLine) from GetLineNumber and
            // editor breakpoints line up (script-debugger.md P1.5). The LANGUAGE framing (any
            // prelude/base a backend needs) stays the backend's job - the run host only supplies
            // the ordered {sourceName, source} pairs, no language syntax here (scripting.md §7.5).
            m_classSourceScratch.Clear();
            m_classSourceScratch.Reserve(m_loadedClasses.Size());
            for (const RefPtr<ScriptClass>& loaded : m_loadedClasses)
            {
                m_classSourceScratch.PushBack(
                    BehaviorModuleClass{loaded->sourceName.AsView(), loaded->source.AsView()});
            }
            ++m_generation;
            const String moduleName = Format(u8"behaviors#{}", m_generation);
            if (!m_context
                     ->LoadBehaviorModule(
                         Span<const BehaviorModuleClass>{m_classSourceScratch.Data(),
                                                         m_classSourceScratch.Size()},
                         moduleName.AsView())
                     .IsOk())
            {
                DRACONIC_LOG_ERROR(u8"Script",
                                   u8"behaviors module failed to compile (class '{}' newly added)",
                                   scriptClass.className);
                return false;
            }
            m_moduleCurrent = true;
            return true;
        }

        /// Total run teardown (the Stop bracket): drops the context, the manager, and
        /// every loaded class reference. Live ScriptObjects held elsewhere keep the
        /// context alive until released - callers destroy instances FIRST.
        void Teardown()
        {
            if (m_context.Get() == nullptr && m_manager.Get() == nullptr)
            {
                return;
            }
            if (m_context.Get() != nullptr)
            {
                m_context->SetErrorHandler(nullptr);
            }
            // The debugger holds engine contexts (a paused one) - release it BEFORE the
            // manager/engine it borrows. Clear the REQUEST + configurator too: they capture the
            // caller (an editor GamePage) that may be destroyed before this host is reused, so a
            // stale configurator must never fire on a later EnsureDebugger (dangling-capture UAF).
            m_debugger = nullptr;
            m_debugTracker.Reset();
            m_debuggerRequested = false;
            m_debuggerConfigurator = Function<void(IScriptDebugger&)>{};
            m_context = nullptr;
            m_manager = nullptr;
            m_language = String{};
            m_loadedClasses.Clear();
            m_moduleCurrent = false;
            m_warnedLanguageMismatch = false;
            DRACONIC_LOG_DEBUG(u8"Script", u8"run script context torn down");
        }

        [[nodiscard]] bool IsActive() const noexcept { return m_context.Get() != nullptr; }

    private:
        // Create the debugger once the manager exists and it was requested (idempotent). The
        // internal tracker is always the listener; the caller's configurator applies the
        // initial breakpoints + grabs the pointer for live changes.
        void EnsureDebugger()
        {
            if (m_debugger || !m_debuggerRequested || m_manager.Get() == nullptr)
            {
                return;
            }
            if (!HasScriptCapability(m_manager->Capabilities(), ScriptCapabilities::Debugger))
            {
                return;
            }
            m_debugger = m_manager->CreateDebugger();
            if (!m_debugger)
            {
                return;
            }
            m_debugger->SetListener(&m_debugTracker);
            if (m_debuggerConfigurator)
            {
                m_debuggerConfigurator(*m_debugger);
            }
        }

        void WarnLanguageMismatchOnce(StringView languageId)
        {
            if (m_warnedLanguageMismatch)
            {
                return;
            }
            m_warnedLanguageMismatch = true;
            DRACONIC_LOG_ERROR(u8"Script",
                               u8"behavior language '{}' differs from the run context's '{}' - one "
                               u8"gameplay context per run; these behaviors stay disabled",
                               languageId, m_language);
        }

        RefPtr<IScriptManager> m_manager;
        RefPtr<IScriptContext> m_context;
        String m_language;
        RunScriptErrorSink m_errorSink;
        ScriptRuntimeBinding m_binding;
        Function<void(IScriptContext&)> m_configurator;
        Array<RefPtr<ScriptClass>> m_loadedClasses; // the behaviors module's content
        Array<BehaviorModuleClass>
            m_classSourceScratch;              // reused per-rebuild {sourceName, source} list
        UniquePtr<IScriptDebugger> m_debugger; // the run's step debugger (opt-in)
        DebugPauseTracker m_debugTracker;      // the game-pause flag + external forward
        Function<void(IScriptDebugger&)> m_debuggerConfigurator;
        u32 m_generation = 0;
        bool m_moduleCurrent = false;
        bool m_warnedLanguageMismatch = false;
        bool m_debuggerRequested = false;
        bool m_gameScriptHold = false; // a game script is loaded into this host (teardown pin)
    };

    // ---- per-scene dispatch ----

    class ScriptSceneSystem final : public scene::SceneSystem
    {
    public:
        void OnSceneCreate(scene::Scene& scene) override { m_scene = &scene; }

        /// The subsystem (or a headless test) wires the shared run host in.
        void SetRunHost(ScriptRunHost* host) noexcept { m_host = host; }
        [[nodiscard]] ScriptRunHost* Host() const noexcept { return m_host; }
        /// Optional: the subsystem hears about run-participation changes (teardown check).
        void SetRunObserver(Function<void()> observer) { m_runObserver = Move(observer); }

        [[nodiscard]] bool Started() const noexcept { return m_started; }
        [[nodiscard]] scene::Scene* ScenePtr() const noexcept { return m_scene; }

        // Behaviors tick ONLY under simulation (Simulate toggle and PIE both count).
        [[nodiscard]] bool IsSimulationOnly() const noexcept override { return true; }

        void OnSceneStarted() override { m_started = true; }

        void OnSceneStopped() override
        {
            m_started = false;
            DestroyAllInstances();
            if (m_runObserver)
            {
                m_runObserver();
            }
        }

        void OnUpdate(scene::ScenePhase phase, f32 deltaTime) override
        {
            if (phase != scene::ScenePhase::Update)
            {
                return;
            }
            if (!m_started || m_scene == nullptr || m_host == nullptr)
            {
                return;
            }
            // Frozen at a breakpoint: the world holds still (behaviors + coroutines stop
            // advancing) while the debugger owns a suspended handler. The editor loop keeps
            // running; step/continue drive the held context directly, not this tick.
            if (m_host->IsDebugPaused())
            {
                return;
            }
            m_host->Binding().currentScene = m_scene; // Scene.spawn target for this tick
            TickBehaviors(deltaTime);
            DrainMessages(); // deferred entity.send delivery - same frame, never nested
            // Resume due coroutines ONCE per simulated frame, at the tick's top level (no
            // VM call active - the backend's resume is safe here). Gated to a backend that
            // actually has the scheduler; a non-supporting one no-ops anyway.
            if (IScriptManager* manager = m_host->Manager();
                manager != nullptr &&
                HasScriptCapability(manager->Capabilities(), ScriptCapabilities::Coroutines))
            {
                manager->AdvanceCoroutines(static_cast<f64>(deltaTime));
            }
        }

        /// Live instance count (the subsystem's context-teardown bookkeeping).
        [[nodiscard]] u32 InstanceCount() const
        {
            auto* components =
                m_scene != nullptr ? m_scene->GetSystem<ScriptComponentManager>() : nullptr;
            if (components == nullptr)
            {
                return 0;
            }
            u32 count = 0;
            components->ForEach(
                [&count](ScriptComponent& component, scene::EntityHandle)
                {
                    for (const ScriptBehavior& behavior : component.behaviors)
                    {
                        if (behavior.instance.Get() != nullptr)
                        {
                            ++count;
                        }
                    }
                });
            return count;
        }

        /// onDestroy + release for one component's behaviors (entity/component removal).
        void ReleaseComponentInstances(ScriptComponent& component, scene::EntityHandle entity)
        {
            for (ScriptBehavior& behavior : component.behaviors)
            {
                StopBehavior(behavior, entity, true);
            }
        }

        /// Behavior messaging (P2 §3.4): QUEUE `on<Message>(args)` for EVERY enabled
        /// behavior of `target` that declares the handler. Reached from the `Entity::send`
        /// facade via the run binding's route. Delivery is DEFERRED (drained at the tick's
        /// top level) because a send happens INSIDE a running script call and Wren forbids
        /// re-entrant VM calls - so messages arrive later the same frame, never nested.
        void EnqueueMessage(scene::EntityHandle target, StringView message,
                            Span<const Variant> args)
        {
            if (message.IsEmpty())
            {
                return;
            }
            PendingMessage pending;
            pending.target = target;
            pending.handler = BuildMessageHandlerName(message);
            pending.args.Reserve(args.Size());
            for (const Variant& arg : args)
            {
                pending.args.PushBack(arg);
            }
            m_messages.PushBack(Move(pending));
        }

        /// Physics contacts (§ contact events): QUEUE a contact handler call (the handler name
        /// is already the final `on<Event>` - e.g. "onContactBegin" - not a message to convert)
        /// with pre-marshalled args. Reuses the SAME deferred queue as entity.send so it drains
        /// at the tick's top level: the physics tick pushes contacts (never nested in a script
        /// call), and delivery is gated by HasHandler in InvokeHandler exactly like messages.
        void EnqueueContact(scene::EntityHandle target, StringView handler, Array<Variant> args)
        {
            if (handler.IsEmpty())
            {
                return;
            }
            PendingMessage pending;
            pending.target = target;
            pending.handler = String(handler);
            pending.args = Move(args);
            m_messages.PushBack(Move(pending));
        }

        /// Drains queued messages at the tick's top level (no VM call is active here, so
        /// InvokeHandler's wrenCall is safe). A handler may send again - those are drained
        /// in the same pass, capped to break runaway send loops.
        void DrainMessages()
        {
            if (m_messages.IsEmpty())
            {
                return;
            }
            auto* components =
                m_scene != nullptr ? m_scene->GetSystem<ScriptComponentManager>() : nullptr;
            usize delivered = 0;
            for (usize m = 0; m < m_messages.Size(); ++m)
            {
                if (++delivered > kMaxMessagesPerDrain)
                {
                    DRACONIC_LOG_WARNING(
                        u8"Script",
                        u8"message drain hit the {} cap - dropping the rest (send loop?)",
                        kMaxMessagesPerDrain);
                    break;
                }
                // Copy out before dispatch: delivering may append (reallocating m_messages).
                const scene::EntityHandle target = m_messages[m].target;
                const String handler = m_messages[m].handler;
                Array<Variant> args = m_messages[m].args;
                if (components == nullptr)
                {
                    continue;
                }
                const Span<Variant> argSpan{args.Data(), args.Size()};
                for (usize i = 0;; ++i)
                {
                    ScriptComponent* component = components->Get(target);
                    if (component == nullptr || i >= component->behaviors.Size())
                    {
                        break;
                    }
                    ScriptBehavior& behavior = component->behaviors[i];
                    if (!behavior.enabled || behavior.faulted ||
                        behavior.instance.Get() == nullptr || behavior.boundClass == nullptr)
                    {
                        continue;
                    }
                    (void)InvokeHandler(behavior, *behavior.boundClass, target, handler.AsView(),
                                        argSpan);
                }
                // A message handler that hit a breakpoint pauses the game: stop draining (the
                // rest of this pass is dropped - a rare edge, message-handler breakpoints).
                if (m_host != nullptr && m_host->IsDebugPaused())
                {
                    break;
                }
            }
            m_messages.Clear();
        }

    private:
        // The result of one handler dispatch: Ok (ran), Faulted (disabled), or Suspended
        // (hit a breakpoint - the game is paused, the debugger owns the mid-flight handler).
        enum class HandlerOutcome
        {
            Ok,
            Faulted,
            Suspended
        };

        static constexpr StringView kOnStart = u8"onStart";
        static constexpr StringView kOnUpdate = u8"onUpdate";
        static constexpr StringView kOnEnable = u8"onEnable";
        static constexpr StringView kOnDisable = u8"onDisable";
        static constexpr StringView kOnDestroy = u8"onDestroy";

        // "heal" -> "onHeal": the send() message convention. First char uppercased.
        [[nodiscard]] static String BuildMessageHandlerName(StringView message)
        {
            String name(u8"on");
            for (usize i = 0; i < message.Size(); ++i)
            {
                utf8char c = message[i];
                if (i == 0 && c >= u8'a' && c <= u8'z')
                {
                    c = static_cast<utf8char>(c - 32);
                }
                name += c;
            }
            return name;
        }

        void TickBehaviors(f32 deltaTime)
        {
            auto* components = m_scene->GetSystem<ScriptComponentManager>();
            if (components == nullptr || components->Count() == 0)
            {
                return;
            }

            // Snapshot the owner list: scripts may destroy entities (swap-remove moves
            // pool slots) or spawn new ones (picked up next tick - the deferred start).
            m_tickOwners.Clear();
            for (scene::EntityHandle owner : components->Owners())
            {
                m_tickOwners.PushBack(owner);
            }

            for (scene::EntityHandle entity : m_tickOwners)
            {
                // Re-resolve per behavior: any dispatch can mutate the pool.
                for (usize i = 0;; ++i)
                {
                    ScriptComponent* component = components->Get(entity);
                    if (component == nullptr || i >= component->behaviors.Size())
                    {
                        break;
                    }
                    TickBehavior(component->behaviors[i], entity, deltaTime);
                    // A breakpoint hit inside that dispatch pauses the game mid-tick: stop
                    // advancing the rest of this tick (the world holds still).
                    if (m_host != nullptr && m_host->IsDebugPaused())
                    {
                        return;
                    }
                }
            }
        }

        void TickBehavior(ScriptBehavior& behavior, scene::EntityHandle entity, f32 deltaTime)
        {
            ScriptClass* scriptClass = behavior.script.Get();
            if (scriptClass == nullptr)
            {
                if (behavior.instance.Get() != nullptr)
                {
                    StopBehavior(behavior, entity, true);
                }
                return;
            }

            // Hot reload: the resource handle swapped its product - re-instantiate and
            // re-apply overrides below (transient script state resets; documented v1).
            if (behavior.instance.Get() != nullptr && behavior.boundClass != scriptClass)
            {
                StopBehavior(behavior, entity, false);
                behavior.faulted = false;
            }

            if (!behavior.enabled)
            {
                if (behavior.instance.Get() != nullptr && behavior.active)
                {
                    behavior.active =
                        false; // set before dispatch (no re-dispatch on suspend/fault)
                    (void)InvokeHandler(behavior, *behavior.boundClass, entity, kOnDisable, {});
                    CancelCoroutines(behavior); // a disabled behavior's coroutines stop too
                }
                return;
            }
            if (behavior.faulted)
            {
                return;
            }

            if (behavior.instance.Get() == nullptr)
            {
                InstantiateBehavior(behavior, entity, *scriptClass);
                if (behavior.instance.Get() == nullptr)
                {
                    return;
                }
            }

            // Lifecycle flags are set BEFORE the dispatch: whether it faults OR debug-suspends
            // (mid-handler, game paused), it must not re-dispatch onEnable/onStart. A
            // debug-suspended handler runs to completion later via the debugger's Continue.
            if (!behavior.active)
            {
                behavior.active = true;
                if (InvokeHandler(behavior, *scriptClass, entity, kOnEnable, {}) !=
                    HandlerOutcome::Ok)
                {
                    return;
                }
            }
            if (!behavior.started)
            {
                behavior.started = true;
                if (InvokeHandler(behavior, *scriptClass, entity, kOnStart, {}) !=
                    HandlerOutcome::Ok)
                {
                    return;
                }
            }
            // updateInterval throttling (P3): 0 = every tick with the raw dt; otherwise
            // bank time and deliver once the interval elapses, passing the ACCUMULATED dt
            // (so movement integrates correctly at a lower call rate).
            if (behavior.updateInterval > 0.0f)
            {
                behavior.updateAccumulator += deltaTime;
                if (behavior.updateAccumulator + 1e-6f >= behavior.updateInterval)
                {
                    Variant dt = Variant::From(behavior.updateAccumulator);
                    behavior.updateAccumulator =
                        0.0f; // consume before dispatch (no double on resume)
                    (void)InvokeHandler(behavior, *scriptClass, entity, kOnUpdate,
                                        Span<Variant>{&dt, 1});
                }
            }
            else
            {
                Variant dt = Variant::From(deltaTime);
                (void)InvokeHandler(behavior, *scriptClass, entity, kOnUpdate,
                                    Span<Variant>{&dt, 1});
            }
        }

        void InstantiateBehavior(ScriptBehavior& behavior, scene::EntityHandle entity,
                                 ScriptClass& scriptClass)
        {
            if (scriptClass.className.IsEmpty())
            {
                // A utility module reference: nothing to instantiate; not an error.
                behavior.boundClass = &scriptClass;
                return;
            }
            DRACONIC_PROFILE_SCOPE(scriptClass.ProfileName());
            Entity handle;
            handle.scene = m_scene;
            handle.entityIndex = entity.index;
            handle.entityGeneration = entity.generation;
            Variant arg = Variant::From(handle);
            behavior.instance = m_host->Instantiate(scriptClass, Span<Variant>{&arg, 1});
            behavior.boundClass = &scriptClass;
            behavior.started = false;
            behavior.active = false;
            behavior.updateAccumulator = 0.0f; // fresh instance banks from zero
            if (behavior.instance.Get() == nullptr)
            {
                behavior.faulted = true;
                DRACONIC_LOG_ERROR(
                    u8"Script", u8"'{}': behavior '{}' failed to instantiate - behavior disabled",
                    m_scene->GetEntityName(entity), scriptClass.className);
                return;
            }
            ApplyProperties(behavior, scriptClass, entity);
        }

        // Defaults first, then hash-keyed overrides win; pushed through the class's
        // per-property setter ("<name>=") - Invoke builds the setter call.
        void ApplyProperties(ScriptBehavior& behavior, const ScriptClass& scriptClass,
                             scene::EntityHandle entity)
        {
            for (const ScriptPropertyDesc& property : scriptClass.properties)
            {
                const ScriptPropertyOverride* over = behavior.FindOverride(property.hash);
                const ScriptPropertyValue& value =
                    over != nullptr ? over->value : property.defaultValue;
                Variant marshalled = PropertyValueToVariant(value, property.type);

                String setter(property.name.AsView());
                setter += u8"=";
                Variant args[1] = {Move(marshalled)};
                if (auto result =
                        behavior.instance->Invoke(setter.AsView(), Span<Variant>{args, 1});
                    !result.HasValue())
                {
                    DRACONIC_LOG_WARNING(
                        u8"Script",
                        u8"'{}': class '{}' has no setter '{}=' for its declared property",
                        m_scene->GetEntityName(entity), scriptClass.className, property.name);
                }
            }
        }

        [[nodiscard]] Variant PropertyValueToVariant(const ScriptPropertyValue& value,
                                                     ScriptPropertyType declaredType) const
        {
            const ScriptPropertyType kind =
                value.kind != ScriptPropertyType::None ? value.kind : declaredType;
            switch (kind)
            {
            case ScriptPropertyType::Float:
            case ScriptPropertyType::Int:
                return Variant::From<f64>(value.number);
            case ScriptPropertyType::Bool:
                return Variant::From<bool>(value.boolean);
            case ScriptPropertyType::String:
                return Variant::From<String>(String(value.text.AsView()));
            case ScriptPropertyType::Color:
                return Variant::From<Color>(value.color);
            case ScriptPropertyType::Vec3:
                return Variant::From<Float3>(value.vector);
            case ScriptPropertyType::Entity:
            {
                if (value.guid.IsNil() || m_scene == nullptr)
                {
                    return Variant{};
                }
                const scene::EntityHandle target = m_scene->FindEntity(value.guid);
                if (!target.IsAssigned())
                {
                    return Variant{};
                }
                Entity handle;
                handle.scene = m_scene;
                handle.entityIndex = target.index;
                handle.entityGeneration = target.generation;
                return Variant::From(handle);
            }
            case ScriptPropertyType::Asset:
                return Variant::From<Guid>(value.guid);
            case ScriptPropertyType::None:
            default:
                return Variant{};
            }
        }

        // One handler dispatch: declared-handler gate (no method-missing probing), the
        // per-behavior profile scope, and the fault-disables-this-behavior rule. A handler
        // that DEBUG-SUSPENDED (hit a breakpoint) is reported as Suspended, NOT a fault: the
        // game is now paused and the debugger owns the mid-flight handler; on Continue it runs
        // to completion and normal flow resumes.
        [[nodiscard]] HandlerOutcome InvokeHandler(ScriptBehavior& behavior,
                                                   const ScriptClass& scriptClass,
                                                   scene::EntityHandle entity, StringView method,
                                                   Span<Variant> args)
        {
            if (behavior.instance.Get() == nullptr || !scriptClass.HasHandler(method))
            {
                return HandlerOutcome::Ok;
            }
            DRACONIC_PROFILE_SCOPE(scriptClass.ProfileName());
            auto result = behavior.instance->Invoke(method, args);
            if (result.HasValue())
            {
                return HandlerOutcome::Ok;
            }
            if (m_host != nullptr && m_host->IsDebugPaused())
            {
                return HandlerOutcome::Suspended;
            }
            behavior.faulted = true;
            DRACONIC_LOG_ERROR(
                u8"Script", u8"'{}': behavior '{}' faulted in {} - behavior disabled",
                m_scene != nullptr ? m_scene->GetEntityName(entity) : StringView(u8"?"),
                scriptClass.className, method);
            return HandlerOutcome::Faulted;
        }

        // Stop every coroutine the behavior's instance started (disable / destroy /
        // reload). Gated to a coroutine-capable backend AND a class that opted in
        // (usesCoroutines) - a backend/class without coroutines pays nothing.
        void CancelCoroutines(ScriptBehavior& behavior)
        {
            if (behavior.instance.Get() == nullptr || behavior.boundClass == nullptr ||
                !behavior.boundClass->usesCoroutines || m_host == nullptr)
            {
                return;
            }
            IScriptManager* manager = m_host->Manager();
            if (manager == nullptr ||
                !HasScriptCapability(manager->Capabilities(), ScriptCapabilities::Coroutines))
            {
                return;
            }
            manager->CancelCoroutinesFor(*behavior.instance);
        }

        void StopBehavior(ScriptBehavior& behavior, scene::EntityHandle entity, bool invokeDestroy)
        {
            if (behavior.instance.Get() != nullptr && invokeDestroy && behavior.started &&
                behavior.boundClass != nullptr && !behavior.faulted)
            {
                (void)InvokeHandler(behavior, *behavior.boundClass, entity, kOnDestroy, {});
            }
            CancelCoroutines(behavior); // drop pending coroutines before releasing the instance
            behavior.instance = nullptr;
            behavior.boundClass = nullptr;
            behavior.started = false;
            behavior.active = false;
        }

        void DestroyAllInstances()
        {
            auto* components =
                m_scene != nullptr ? m_scene->GetSystem<ScriptComponentManager>() : nullptr;
            if (components == nullptr)
            {
                return;
            }
            components->ForEach([this](ScriptComponent& component, scene::EntityHandle entity)
                                { ReleaseComponentInstances(component, entity); });
        }

        struct PendingMessage
        {
            scene::EntityHandle target;
            String handler;      // prebuilt "on<Message>"
            Array<Variant> args; // marshalled at send time
        };
        static constexpr usize kMaxMessagesPerDrain = 4096;

        scene::Scene* m_scene = nullptr;
        ScriptRunHost* m_host = nullptr;
        Function<void()> m_runObserver;
        Array<scene::EntityHandle> m_tickOwners; // per-tick snapshot (reused)
        Array<PendingMessage> m_messages;        // deferred entity.send queue
        bool m_started = false;
    };

    // ---- the runtime subsystem ----

    /// The neutral contact vocabulary the script layer speaks. A producer (physics, via a
    /// composition-root bridge) maps its own kind onto this - the script subsystem never
    /// names a physics type, so it does not depend on the physics library.
    enum class ScriptContactKind : u8
    {
        Begin,
        End,
        TriggerEnter,
        TriggerExit
    };

    class ScriptSubsystem final : public draconic::runtime::Subsystem, public scene::ISceneAware
    {
    public:
        /// The DEFAULT run host - the one for the editor's editing/loose scenes (game-instance.md
        /// §11.10). A GameInstance owns its OWN run host for its game scenes + game script; this is not
        /// that. Editing-scene Simulate runs its behaviors on this host.
        [[nodiscard]] ScriptRunHost& RunHost() noexcept { return m_ownedRunHost; }

        /// Wire a run host (this default OR a GameInstance's) with the app services + routing so its
        /// context, once created, has the facades, the Scene.spawn spawner, and entity.send routing
        /// (game-instance.md §11.10). The wrappers read the stored configurator/spawner + m_systems
        /// LIVE, so one call per host suffices and the same message route serves every host. Call
        /// once per run host before its first context is created.
        void ConfigureRunHost(ScriptRunHost& host)
        {
            ScriptSubsystem* self = this;
            host.SetContextConfigurator(
                Function<void(IScriptContext&)>{[self](IScriptContext& context)
                                                {
                                                    if (self->m_configurator)
                                                    {
                                                        self->m_configurator(context);
                                                    }
                                                }});
            host.Binding().spawnPrefab =
                Function<scene::EntityHandle(scene::Scene*, const Guid&, const Float3&)>{
                    [self](scene::Scene* scene, const Guid& prefab,
                           const Float3& position) -> scene::EntityHandle
                    {
                        return self->m_spawner ? self->m_spawner(scene, prefab, position)
                                               : scene::EntityHandle::Invalid();
                    }};
            host.Binding().dispatchMessage =
                Function<void(scene::Scene*, scene::EntityHandle, StringView, Span<const Variant>)>{
                    [self](scene::Scene* scene, scene::EntityHandle target, StringView message,
                           Span<const Variant> args)
                    {
                        for (const SceneEntry& entry : self->m_systems)
                        {
                            if (entry.scene == scene && entry.system != nullptr)
                            {
                                entry.system->EnqueueMessage(target, message, args);
                                return;
                            }
                        }
                    }};
        }

        /// Tear down `host` if nothing pins it: no game-script hold, no scene BOUND TO IT simulating,
        /// no live behavior instances in those scenes (game-instance.md §11.10). Per-host, so an
        /// instance's host and the editor's host tear down independently. Called by the scene-stop
        /// observer, OnSceneDestroyed, and a GameInstance on its own host.
        void MaybeTeardownRunHost(ScriptRunHost& host)
        {
            if (!host.IsActive() || host.HasGameScriptHold())
            {
                return;
            }
            for (const SceneEntry& entry : m_systems)
            {
                if (entry.system == nullptr || entry.scene == nullptr)
                {
                    continue;
                }
                if (entry.system->Host() != &host)
                {
                    continue;
                } // only scenes bound to THIS host
                if (entry.system->Started() && entry.scene->SimulationEnabled())
                {
                    return;
                }
                if (entry.system->InstanceCount() > 0)
                {
                    return;
                }
            }
            host.Teardown();
        }

        // ---- contact events (neutral ingress) ----

        /// Deliver a resolved contact to BOTH entities' declared handlers (each sees the
        /// OTHER as an Entity). Collision kinds get (other, point, normal, speed); trigger
        /// kinds get (other). Physics-agnostic: the host bridges physics contacts to this.
        /// Only ENQUEUES onto the owning scene's deferred queue - drained at the scene
        /// tick's top level, so no re-entrancy even though physics stepped this frame.
        void DeliverContact(scene::Scene* scene, scene::EntityHandle a, scene::EntityHandle b,
                            ScriptContactKind kind, const Float3& point, const Float3& normal,
                            f32 speed)
        {
            StringView handler;
            bool trigger = false;
            switch (kind)
            {
            case ScriptContactKind::Begin:
                handler = u8"onContactBegin";
                break;
            case ScriptContactKind::End:
                handler = u8"onContactEnd";
                break;
            case ScriptContactKind::TriggerEnter:
                handler = u8"onTriggerEnter";
                trigger = true;
                break;
            case ScriptContactKind::TriggerExit:
                handler = u8"onTriggerExit";
                trigger = true;
                break;
            }
            ScriptSceneSystem* system = SystemForScene(scene);
            if (system == nullptr)
            {
                return;
            }
            DeliverContactSide(*system, scene, a, b, handler, point, normal, speed, trigger);
            DeliverContactSide(*system, scene, b, a, handler, point, normal, speed, trigger);
        }

        /// Host-app wiring: exposes engine services (Input/Audio/Physics facades) on every run context
        /// (via ConfigureRunHost's live wrapper - applies to the default host AND every instance host).
        void SetContextConfigurator(Function<void(IScriptContext&)> configurator)
        {
            m_configurator = Move(configurator);
        }
        /// Host-app wiring: the prefab spawner behind `Scene.spawn` (the host owns the content DB that
        /// resolves a prefab id). Applied to every run host by ConfigureRunHost's live wrapper.
        void SetPrefabSpawner(
            Function<scene::EntityHandle(scene::Scene*, const Guid&, const Float3&)> spawner)
        {
            m_spawner = Move(spawner);
        }
        // The game-script run context is no longer acquired here (game-instance.md §11.10): a
        // GameInstance owns its run host and drives its own game script through it. This subsystem is
        // machinery (routing + ISceneAware + reflection + Configure/MaybeTeardownRunHost).

        // ---- scene integration ----

        void OnSceneCreated(scene::Scene& scene) override
        {
            auto* components = scene.AddSystem<ScriptComponentManager>();
            ScriptSceneSystem* system = scene.AddSystem<ScriptSceneSystem>();
            components->SetScriptSystem(system);
            // Bind to the DEFAULT run host; a GameInstance re-binds ITS scenes to its own host on adopt
            // (game-instance.md §11.10). The teardown observer checks the system's CURRENT host.
            system->SetRunHost(&m_ownedRunHost);
            ScriptSubsystem* self = this;
            system->SetRunObserver(Function<void()>{[self, system]()
                                                    {
                                                        if (system->Host() != nullptr)
                                                        {
                                                            self->MaybeTeardownRunHost(
                                                                *system->Host());
                                                        }
                                                    }});
            m_systems.PushBack(SceneEntry{&scene, system});
        }
        void OnSceneDestroyed(scene::Scene& scene) override
        {
            ScriptRunHost* host = nullptr;
            for (usize i = 0; i < m_systems.Size(); ++i)
            {
                if (m_systems[i].scene == &scene)
                {
                    if (m_systems[i].system != nullptr)
                    {
                        host = m_systems[i].system->Host();
                    }
                    m_systems.RemoveAt(i);
                    break;
                }
            }
            if (host != nullptr)
            {
                MaybeTeardownRunHost(*host);
            }
        }

        // Drives the DEFAULT run host (editor scenes). A GameInstance drives its own host.
        void Update(f32 deltaTime) override
        {
            ScriptRuntimeBinding& binding = m_ownedRunHost.Binding();
            binding.timeSeconds += static_cast<f64>(deltaTime);
            binding.deltaSeconds = deltaTime;
            if (m_ownedRunHost.Manager() != nullptr)
            {
                m_ownedRunHost.Manager()->CollectGarbage(); // frame-budgeted GC stepping
            }
        }

    protected:
        void OnInit() override
        {
            RegisterScriptComponentReflection();
            RegisterScriptFacadeReflection();
        }
        void OnReady() override
        {
            ConfigureRunHost(m_ownedRunHost); // wire the default (editor-scene) run host once
            if (draconic::runtime::Context* context = GetContext())
            {
                if (auto* scenes = context->GetSubsystem<scene::SceneSubsystem>())
                {
                    scenes->RegisterSceneAware(this);
                }
            }
        }
        void OnShutdown() override
        {
            if (draconic::runtime::Context* context = GetContext())
            {
                if (auto* scenes = context->GetSubsystem<scene::SceneSubsystem>())
                {
                    scenes->UnregisterSceneAware(this);
                }
            }
            for (const SceneEntry& entry : m_systems)
            {
                // Instances hold the context alive - release them before the host.
                if (entry.system != nullptr && entry.scene != nullptr)
                {
                    entry.system->OnSceneStopped();
                }
            }
            m_ownedRunHost.Teardown(); // instance hosts are torn down by their owners
        }

    private:
        struct SceneEntry
        {
            scene::Scene* scene = nullptr;
            ScriptSceneSystem* system = nullptr;
        };

        [[nodiscard]] ScriptSceneSystem* SystemForScene(scene::Scene* scene)
        {
            for (const SceneEntry& entry : m_systems)
            {
                if (entry.scene == scene)
                {
                    return entry.system;
                }
            }
            return nullptr;
        }

        // Enqueue one side of a contact: `self` receives the handler with `other` marshalled as
        // an Entity (collision handlers also get point/normal/speed). Skips a side whose entity
        // didn't resolve (invalid `self`); a stale `other` marshals to a safe no-op Entity.
        void DeliverContactSide(ScriptSceneSystem& system, scene::Scene* scene,
                                scene::EntityHandle self, scene::EntityHandle other,
                                StringView handler, const Float3& point, const Float3& normal,
                                f32 speed, bool trigger)
        {
            if (!self.IsAssigned())
            {
                return;
            }
            Entity otherEntity;
            otherEntity.scene = scene;
            otherEntity.entityIndex = other.index;
            otherEntity.entityGeneration = other.generation;
            Array<Variant> args;
            args.PushBack(Variant::From<Entity>(otherEntity));
            if (!trigger)
            {
                args.PushBack(Variant::From<Float3>(point));
                args.PushBack(Variant::From<Float3>(normal));
                args.PushBack(Variant::From<f64>(static_cast<f64>(speed)));
            }
            system.EnqueueContact(self, handler, Move(args));
        }

        ScriptRunHost
            m_ownedRunHost; // the DEFAULT run host (editor/editing scenes; game-instance §11.10)
        Array<SceneEntry> m_systems;
        Function<void(IScriptContext&)>
            m_configurator; // app services, applied to every host via ConfigureRunHost
        Function<scene::EntityHandle(scene::Scene*, const Guid&, const Float3&)>
            m_spawner; // Scene.spawn
    };
}
