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
export import draconic.script.facades;   // Entity/Log/Time/Random + the run-service binding

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
    namespace dscene = draconic::scene;

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
            if (external != nullptr) { external->OnError(error); }
        }
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
                if (m_language.AsView() == languageId) { return m_context.Get(); }
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
                DRACONIC_LOG_ERROR(u8"Script", u8"no script backend for language '{}'",
                                   languageId);
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
            m_binding.random = core::Random{};   // fresh per-run RNG
            m_context->SetErrorHandler(&m_errorSink);
            m_context->SetService(kScriptRuntimeService, &m_binding);
            if (m_configurator) { m_configurator(*m_context); }
            m_moduleCurrent = false;
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
                if (path[i] == u8'/' || path[i] == u8'\\') { break; }
            }
            const ScriptBackendRegistry& registry = ScriptBackendRegistry::Get();
            const ScriptBackendDesc* backend = extension.IsEmpty()
                ? nullptr : registry.FindByExtension(extension);
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
        [[nodiscard]] RefPtr<ScriptObject> Instantiate(ScriptClass& scriptClass,
                                                       Span<Variant> args)
        {
            if (!EnsureClassLoaded(scriptClass)) { return nullptr; }
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
                ? StringView(u8"wren") : scriptClass.language.AsView();
            if (EnsureContext(language) == nullptr) { return false; }

            bool present = false;
            for (usize i = 0; i < m_loadedClasses.Size(); ++i)
            {
                ScriptClass* loaded = m_loadedClasses[i].Get();
                if (loaded == &scriptClass) { present = true; break; }
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
            if (m_moduleCurrent) { return true; }

            // Rebuild: one concatenated module, fresh generation name. The LANGUAGE
            // framing (any prelude/base a backend needs) is the backend's job - the run
            // host only supplies the ordered class SOURCES and compiles the result, so no
            // language syntax lives here (scripting.md §7.5).
            m_classSourceScratch.Clear();
            m_classSourceScratch.Reserve(m_loadedClasses.Size());
            for (const RefPtr<ScriptClass>& loaded : m_loadedClasses)
            {
                m_classSourceScratch.PushBack(loaded->source.AsView());
            }
            const String moduleSource = m_manager->AssembleBehaviorModuleSource(
                Span<const StringView>{ m_classSourceScratch.Data(), m_classSourceScratch.Size() });
            ++m_generation;
            const String moduleName = Format(u8"behaviors#{}", m_generation);
            if (!m_context->Load(moduleSource.AsView(), moduleName.AsView()).IsOk())
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
            if (m_context.Get() == nullptr && m_manager.Get() == nullptr) { return; }
            if (m_context.Get() != nullptr) { m_context->SetErrorHandler(nullptr); }
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
        void WarnLanguageMismatchOnce(StringView languageId)
        {
            if (m_warnedLanguageMismatch) { return; }
            m_warnedLanguageMismatch = true;
            DRACONIC_LOG_ERROR(u8"Script",
                u8"behavior language '{}' differs from the run context's '{}' - one "
                u8"gameplay context per run; these behaviors stay disabled", languageId,
                m_language);
        }

        RefPtr<IScriptManager> m_manager;
        RefPtr<IScriptContext> m_context;
        String m_language;
        RunScriptErrorSink m_errorSink;
        ScriptRuntimeBinding m_binding;
        Function<void(IScriptContext&)> m_configurator;
        Array<RefPtr<ScriptClass>> m_loadedClasses;   // the behaviors module's content
        Array<StringView> m_classSourceScratch;       // reused per-rebuild source view list
        u32 m_generation = 0;
        bool m_moduleCurrent = false;
        bool m_warnedLanguageMismatch = false;
    };

    // ---- per-scene dispatch ----

    class ScriptSceneSystem final : public dscene::SceneSystem
    {
    public:
        void OnSceneCreate(dscene::Scene& scene) override { m_scene = &scene; }

        /// The subsystem (or a headless test) wires the shared run host in.
        void SetRunHost(ScriptRunHost* host) noexcept { m_host = host; }
        /// Optional: the subsystem hears about run-participation changes (teardown check).
        void SetRunObserver(Function<void()> observer) { m_runObserver = Move(observer); }

        [[nodiscard]] bool Started() const noexcept { return m_started; }
        [[nodiscard]] dscene::Scene* ScenePtr() const noexcept { return m_scene; }

        // Behaviors tick ONLY under simulation (Simulate toggle and PIE both count).
        [[nodiscard]] bool IsSimulationOnly() const noexcept override { return true; }

        void OnSceneStarted() override { m_started = true; }

        void OnSceneStopped() override
        {
            m_started = false;
            DestroyAllInstances();
            if (m_runObserver) { m_runObserver(); }
        }

        void OnUpdate(dscene::ScenePhase phase, f32 deltaTime) override
        {
            if (phase != dscene::ScenePhase::Update) { return; }
            if (!m_started || m_scene == nullptr || m_host == nullptr) { return; }
            m_host->Binding().currentScene = m_scene;   // Scene.spawn target for this tick
            TickBehaviors(deltaTime);
            DrainMessages();   // deferred entity.send delivery - same frame, never nested
            // Resume due coroutines ONCE per simulated frame, at the tick's top level (no
            // VM call active - the backend's resume is safe here). Gated to a backend that
            // actually has the scheduler; a non-supporting one no-ops anyway.
            if (IScriptManager* manager = m_host->Manager();
                manager != nullptr
                && HasScriptCapability(manager->Capabilities(), ScriptCapabilities::Coroutines))
            {
                manager->AdvanceCoroutines(static_cast<f64>(deltaTime));
            }
        }

        /// Live instance count (the subsystem's context-teardown bookkeeping).
        [[nodiscard]] u32 InstanceCount() const
        {
            auto* components = m_scene != nullptr
                ? m_scene->GetSystem<ScriptComponentManager>() : nullptr;
            if (components == nullptr) { return 0; }
            u32 count = 0;
            components->ForEach([&count](ScriptComponent& component, dscene::EntityHandle) {
                for (const ScriptBehavior& behavior : component.behaviors)
                {
                    if (behavior.instance.Get() != nullptr) { ++count; }
                }
            });
            return count;
        }

        /// onDestroy + release for one component's behaviors (entity/component removal).
        void ReleaseComponentInstances(ScriptComponent& component, dscene::EntityHandle entity)
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
        void EnqueueMessage(dscene::EntityHandle target, StringView message,
                            Span<const Variant> args)
        {
            if (message.IsEmpty()) { return; }
            PendingMessage pending;
            pending.target = target;
            pending.handler = BuildMessageHandlerName(message);
            pending.args.Reserve(args.Size());
            for (const Variant& arg : args) { pending.args.PushBack(arg); }
            m_messages.PushBack(Move(pending));
        }

        /// Physics contacts (§ contact events): QUEUE a contact handler call (the handler name
        /// is already the final `on<Event>` - e.g. "onContactBegin" - not a message to convert)
        /// with pre-marshalled args. Reuses the SAME deferred queue as entity.send so it drains
        /// at the tick's top level: the physics tick pushes contacts (never nested in a script
        /// call), and delivery is gated by HasHandler in InvokeHandler exactly like messages.
        void EnqueueContact(dscene::EntityHandle target, StringView handler, Array<Variant> args)
        {
            if (handler.IsEmpty()) { return; }
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
            if (m_messages.IsEmpty()) { return; }
            auto* components = m_scene != nullptr
                ? m_scene->GetSystem<ScriptComponentManager>() : nullptr;
            usize delivered = 0;
            for (usize m = 0; m < m_messages.Size(); ++m)
            {
                if (++delivered > kMaxMessagesPerDrain)
                {
                    DRACONIC_LOG_WARNING(u8"Script",
                        u8"message drain hit the {} cap - dropping the rest (send loop?)",
                        kMaxMessagesPerDrain);
                    break;
                }
                // Copy out before dispatch: delivering may append (reallocating m_messages).
                const dscene::EntityHandle target = m_messages[m].target;
                const String handler = m_messages[m].handler;
                Array<Variant> args = m_messages[m].args;
                if (components == nullptr) { continue; }
                const Span<Variant> argSpan{ args.Data(), args.Size() };
                for (usize i = 0;; ++i)
                {
                    ScriptComponent* component = components->Get(target);
                    if (component == nullptr || i >= component->behaviors.Size()) { break; }
                    ScriptBehavior& behavior = component->behaviors[i];
                    if (!behavior.enabled || behavior.faulted
                        || behavior.instance.Get() == nullptr || behavior.boundClass == nullptr)
                    {
                        continue;
                    }
                    InvokeHandler(behavior, *behavior.boundClass, target, handler.AsView(),
                                  argSpan);
                }
            }
            m_messages.Clear();
        }

    private:
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
                if (i == 0 && c >= u8'a' && c <= u8'z') { c = static_cast<utf8char>(c - 32); }
                name += c;
            }
            return name;
        }

        void TickBehaviors(f32 deltaTime)
        {
            auto* components = m_scene->GetSystem<ScriptComponentManager>();
            if (components == nullptr || components->Count() == 0) { return; }

            // Snapshot the owner list: scripts may destroy entities (swap-remove moves
            // pool slots) or spawn new ones (picked up next tick - the deferred start).
            m_tickOwners.Clear();
            for (dscene::EntityHandle owner : components->Owners())
            {
                m_tickOwners.PushBack(owner);
            }

            for (dscene::EntityHandle entity : m_tickOwners)
            {
                // Re-resolve per behavior: any dispatch can mutate the pool.
                for (usize i = 0;; ++i)
                {
                    ScriptComponent* component = components->Get(entity);
                    if (component == nullptr || i >= component->behaviors.Size()) { break; }
                    TickBehavior(component->behaviors[i], entity, deltaTime);
                }
            }
        }

        void TickBehavior(ScriptBehavior& behavior, dscene::EntityHandle entity, f32 deltaTime)
        {
            ScriptClass* scriptClass = behavior.script.Get();
            if (scriptClass == nullptr)
            {
                if (behavior.instance.Get() != nullptr) { StopBehavior(behavior, entity, true); }
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
                    InvokeHandler(behavior, *behavior.boundClass, entity, kOnDisable, {});
                    behavior.active = false;
                    CancelCoroutines(behavior);   // a disabled behavior's coroutines stop too
                }
                return;
            }
            if (behavior.faulted) { return; }

            if (behavior.instance.Get() == nullptr)
            {
                InstantiateBehavior(behavior, entity, *scriptClass);
                if (behavior.instance.Get() == nullptr) { return; }
            }

            if (!behavior.active)
            {
                InvokeHandler(behavior, *scriptClass, entity, kOnEnable, {});
                behavior.active = true;
                if (behavior.faulted) { return; }
            }
            if (!behavior.started)
            {
                InvokeHandler(behavior, *scriptClass, entity, kOnStart, {});
                behavior.started = true;
                if (behavior.faulted) { return; }
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
                    InvokeHandler(behavior, *scriptClass, entity, kOnUpdate,
                                  Span<Variant>{ &dt, 1 });
                    behavior.updateAccumulator = 0.0f;
                }
            }
            else
            {
                Variant dt = Variant::From(deltaTime);
                InvokeHandler(behavior, *scriptClass, entity, kOnUpdate,
                              Span<Variant>{ &dt, 1 });
            }
        }

        void InstantiateBehavior(ScriptBehavior& behavior, dscene::EntityHandle entity,
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
            behavior.instance = m_host->Instantiate(scriptClass, Span<Variant>{ &arg, 1 });
            behavior.boundClass = &scriptClass;
            behavior.started = false;
            behavior.active = false;
            behavior.updateAccumulator = 0.0f;   // fresh instance banks from zero
            if (behavior.instance.Get() == nullptr)
            {
                behavior.faulted = true;
                DRACONIC_LOG_ERROR(u8"Script",
                    u8"'{}': behavior '{}' failed to instantiate - behavior disabled",
                    m_scene->GetEntityName(entity), scriptClass.className);
                return;
            }
            ApplyProperties(behavior, scriptClass, entity);
        }

        // Defaults first, then hash-keyed overrides win; pushed through the class's
        // per-property setter ("<name>=") - Invoke builds the setter call.
        void ApplyProperties(ScriptBehavior& behavior, const ScriptClass& scriptClass,
                             dscene::EntityHandle entity)
        {
            for (const ScriptPropertyDesc& property : scriptClass.properties)
            {
                const ScriptPropertyOverride* over = behavior.FindOverride(property.hash);
                const ScriptPropertyValue& value =
                    over != nullptr ? over->value : property.defaultValue;
                Variant marshalled = PropertyValueToVariant(value, property.type);

                String setter(property.name.AsView());
                setter += u8"=";
                Variant args[1] = { Move(marshalled) };
                if (auto result = behavior.instance->Invoke(setter.AsView(),
                                                            Span<Variant>{ args, 1 });
                    !result.HasValue())
                {
                    DRACONIC_LOG_WARNING(u8"Script",
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
                    if (value.guid.IsNil() || m_scene == nullptr) { return Variant{}; }
                    const dscene::EntityHandle target = m_scene->FindEntity(value.guid);
                    if (!target.IsAssigned()) { return Variant{}; }
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
        // per-behavior profile scope, and the fault-disables-this-behavior rule.
        void InvokeHandler(ScriptBehavior& behavior, const ScriptClass& scriptClass,
                           dscene::EntityHandle entity, StringView method, Span<Variant> args)
        {
            if (behavior.instance.Get() == nullptr || !scriptClass.HasHandler(method)) { return; }
            DRACONIC_PROFILE_SCOPE(scriptClass.ProfileName());
            if (auto result = behavior.instance->Invoke(method, args); !result.HasValue())
            {
                behavior.faulted = true;
                DRACONIC_LOG_ERROR(u8"Script",
                    u8"'{}': behavior '{}' faulted in {} - behavior disabled",
                    m_scene != nullptr ? m_scene->GetEntityName(entity) : StringView(u8"?"),
                    scriptClass.className, method);
            }
        }

        // Stop every coroutine the behavior's instance started (disable / destroy /
        // reload). Gated to a coroutine-capable backend AND a class that opted in
        // (usesCoroutines) - a backend/class without coroutines pays nothing.
        void CancelCoroutines(ScriptBehavior& behavior)
        {
            if (behavior.instance.Get() == nullptr || behavior.boundClass == nullptr
                || !behavior.boundClass->usesCoroutines || m_host == nullptr)
            {
                return;
            }
            IScriptManager* manager = m_host->Manager();
            if (manager == nullptr
                || !HasScriptCapability(manager->Capabilities(), ScriptCapabilities::Coroutines))
            {
                return;
            }
            manager->CancelCoroutinesFor(*behavior.instance);
        }

        void StopBehavior(ScriptBehavior& behavior, dscene::EntityHandle entity,
                          bool invokeDestroy)
        {
            if (behavior.instance.Get() != nullptr && invokeDestroy && behavior.started
                && behavior.boundClass != nullptr && !behavior.faulted)
            {
                InvokeHandler(behavior, *behavior.boundClass, entity, kOnDestroy, {});
            }
            CancelCoroutines(behavior);   // drop pending coroutines before releasing the instance
            behavior.instance = nullptr;
            behavior.boundClass = nullptr;
            behavior.started = false;
            behavior.active = false;
        }

        void DestroyAllInstances()
        {
            auto* components = m_scene != nullptr
                ? m_scene->GetSystem<ScriptComponentManager>() : nullptr;
            if (components == nullptr) { return; }
            components->ForEach([this](ScriptComponent& component, dscene::EntityHandle entity) {
                ReleaseComponentInstances(component, entity);
            });
        }

        struct PendingMessage
        {
            dscene::EntityHandle target;
            String handler;          // prebuilt "on<Message>"
            Array<Variant> args;     // marshalled at send time
        };
        static constexpr usize kMaxMessagesPerDrain = 4096;

        dscene::Scene* m_scene = nullptr;
        ScriptRunHost* m_host = nullptr;
        Function<void()> m_runObserver;
        Array<dscene::EntityHandle> m_tickOwners;   // per-tick snapshot (reused)
        Array<PendingMessage> m_messages;           // deferred entity.send queue
        bool m_started = false;
    };

    // ---- the runtime subsystem ----

    /// The neutral contact vocabulary the script layer speaks. A producer (physics, via a
    /// composition-root bridge) maps its own kind onto this - the script subsystem never
    /// names a physics type, so it does not depend on the physics library.
    enum class ScriptContactKind : u8 { Begin, End, TriggerEnter, TriggerExit };

    class ScriptSubsystem final : public draconic::runtime::Subsystem,
                                  public dscene::ISceneAware
    {
    public:
        [[nodiscard]] ScriptRunHost& RunHost() noexcept { return m_runHost; }

        // ---- contact events (neutral ingress) ----

        /// Deliver a resolved contact to BOTH entities' declared handlers (each sees the
        /// OTHER as an Entity). Collision kinds get (other, point, normal, speed); trigger
        /// kinds get (other). Physics-agnostic: the host bridges physics contacts to this.
        /// Only ENQUEUES onto the owning scene's deferred queue - drained at the scene
        /// tick's top level, so no re-entrancy even though physics stepped this frame.
        void DeliverContact(dscene::Scene* scene, dscene::EntityHandle a, dscene::EntityHandle b,
                            ScriptContactKind kind, const Float3& point, const Float3& normal,
                            f32 speed)
        {
            StringView handler;
            bool trigger = false;
            switch (kind)
            {
                case ScriptContactKind::Begin:        handler = u8"onContactBegin"; break;
                case ScriptContactKind::End:          handler = u8"onContactEnd"; break;
                case ScriptContactKind::TriggerEnter: handler = u8"onTriggerEnter"; trigger = true; break;
                case ScriptContactKind::TriggerExit:  handler = u8"onTriggerExit"; trigger = true; break;
            }
            ScriptSceneSystem* system = SystemForScene(scene);
            if (system == nullptr) { return; }
            DeliverContactSide(*system, scene, a, b, handler, point, normal, speed, trigger);
            DeliverContactSide(*system, scene, b, a, handler, point, normal, speed, trigger);
        }

        /// Host-app wiring: exposes engine services (Input/Audio/Physics facades) on
        /// every run context the host creates. Set BEFORE the first run.
        void SetContextConfigurator(Function<void(IScriptContext&)> configurator)
        {
            m_runHost.SetContextConfigurator(Move(configurator));
        }
        /// Optional per-run external error sink (the editor's notices). Cleared on release.
        void SetExternalErrorSink(IScriptErrorHandler* sink) noexcept
        {
            m_runHost.SetExternalErrorSink(sink);
        }
        /// Host-app wiring: the prefab spawner behind `Scene.spawn` (the host owns the
        /// content DB that resolves a prefab id to its payload). Set BEFORE the first run.
        void SetPrefabSpawner(Function<dscene::EntityHandle(dscene::Scene*, const Guid&,
                                                            const Float3&)> spawner)
        {
            m_runHost.Binding().spawnPrefab = Move(spawner);
        }

        // ---- the game-script seam (DefaultApplication): SHARES the run context ----

        /// The run context for the game script (resolved by the script file's
        /// extension); holds the context alive until ReleaseRunContext.
        [[nodiscard]] IScriptContext* AcquireRunContextForFile(StringView path)
        {
            IScriptContext* context = m_runHost.EnsureContextForFile(path);
            if (context != nullptr) { m_gameScriptHold = true; }
            return context;
        }
        /// The game script loaded its module into the shared context.
        void NoteExternalLoad() noexcept { m_runHost.NoteExternalLoad(); }
        /// Releases the game script's hold; tears the context down when nothing else
        /// keeps the run alive (the Stop bracket).
        void ReleaseRunContext()
        {
            m_gameScriptHold = false;
            m_runHost.SetExternalErrorSink(nullptr);
            MaybeTeardownRunContext();
        }

        // ---- scene integration ----

        void OnSceneCreated(dscene::Scene& scene) override
        {
            auto* components = scene.AddSystem<ScriptComponentManager>();
            ScriptSceneSystem* system = scene.AddSystem<ScriptSceneSystem>();
            components->SetScriptSystem(system);
            system->SetRunHost(&m_runHost);
            ScriptSubsystem* self = this;
            system->SetRunObserver(Function<void()>{ [self]() {
                self->MaybeTeardownRunContext();
            } });
            m_systems.PushBack(SceneEntry{ &scene, system });
            EnsureMessageRoute();
        }
        void OnSceneDestroyed(dscene::Scene& scene) override
        {
            for (usize i = 0; i < m_systems.Size(); ++i)
            {
                if (m_systems[i].scene == &scene)
                {
                    m_systems.RemoveAt(i);
                    break;
                }
            }
            MaybeTeardownRunContext();
        }

        void Update(f32 deltaTime) override
        {
            ScriptRuntimeBinding& binding = m_runHost.Binding();
            binding.timeSeconds += static_cast<f64>(deltaTime);
            binding.deltaSeconds = deltaTime;
            if (m_runHost.Manager() != nullptr)
            {
                m_runHost.Manager()->CollectGarbage();   // frame-budgeted GC stepping
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
            if (draconic::runtime::Context* context = GetContext())
            {
                if (auto* scenes = context->GetSubsystem<dscene::SceneSubsystem>())
                {
                    scenes->RegisterSceneAware(this);
                }
            }
        }
        void OnShutdown() override
        {
            if (draconic::runtime::Context* context = GetContext())
            {
                if (auto* scenes = context->GetSubsystem<dscene::SceneSubsystem>())
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
            m_runHost.Teardown();
        }

    private:
        struct SceneEntry
        {
            dscene::Scene* scene = nullptr;
            ScriptSceneSystem* system = nullptr;
        };

        [[nodiscard]] ScriptSceneSystem* SystemForScene(dscene::Scene* scene)
        {
            for (const SceneEntry& entry : m_systems)
            {
                if (entry.scene == scene) { return entry.system; }
            }
            return nullptr;
        }

        // Enqueue one side of a contact: `self` receives the handler with `other` marshalled as
        // an Entity (collision handlers also get point/normal/speed). Skips a side whose entity
        // didn't resolve (invalid `self`); a stale `other` marshals to a safe no-op Entity.
        void DeliverContactSide(ScriptSceneSystem& system, dscene::Scene* scene,
                                dscene::EntityHandle self, dscene::EntityHandle other,
                                StringView handler, const Float3& point, const Float3& normal,
                                f32 speed, bool trigger)
        {
            if (!self.IsAssigned()) { return; }
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

        // Route entity.send messages to the scene system that owns the target's scene.
        // Reads the live m_systems list on each call, so a destroyed scene's system is
        // never touched (no dangling capture). Installed once, lazily.
        void EnsureMessageRoute()
        {
            if (m_messageRouteInstalled) { return; }
            ScriptSubsystem* self = this;
            m_runHost.Binding().dispatchMessage = Function<void(dscene::Scene*,
                dscene::EntityHandle, StringView, Span<const Variant>)>{
                [self](dscene::Scene* scene, dscene::EntityHandle target, StringView message,
                       Span<const Variant> args) {
                    for (const SceneEntry& entry : self->m_systems)
                    {
                        if (entry.scene == scene && entry.system != nullptr)
                        {
                            entry.system->EnqueueMessage(target, message, args);
                            return;
                        }
                    }
                } };
            m_messageRouteInstalled = true;
        }

        // The run ends when the game script released its hold, no scene is actively
        // simulating, and no live instances remain (an edit-page scene that is started
        // but frozen never pins the context).
        void MaybeTeardownRunContext()
        {
            if (!m_runHost.IsActive() || m_gameScriptHold) { return; }
            for (const SceneEntry& entry : m_systems)
            {
                if (entry.system == nullptr || entry.scene == nullptr) { continue; }
                if (entry.system->Started() && entry.scene->SimulationEnabled()) { return; }
                if (entry.system->InstanceCount() > 0) { return; }
            }
            m_runHost.Teardown();
        }

        ScriptRunHost m_runHost;
        Array<SceneEntry> m_systems;
        bool m_gameScriptHold = false;
        bool m_messageRouteInstalled = false;
    };
}
