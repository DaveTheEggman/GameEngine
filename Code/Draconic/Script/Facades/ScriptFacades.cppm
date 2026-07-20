// Draconic::ScriptFacades - the `draconic.script.facades` module.
//
// The curated behavior facades (docs/design/scripting.md §3.5): the per-entity
// `Entity` handle behaviors receive as their constructor argument, plus Log/Time/
// Random. A SEPARATE library (below the subsystem) so the COOK's harvest VM and the
// RUNTIME register the SAME "main"-module surface - a facade-using behavior that
// compiles at cook compiles at runtime and vice versa.
//
// Wren visibility rule: reflected classes live in the "main" module; behavior modules
// are separate, so the run host (and the cook's compile check) prepend ONE prelude
// line importing the facade names. Additional engine types (Float3, Color, ...) are
// imported explicitly by the script (`import "main" for Float3`) - both VMs register
// the core types, so the import resolves in both.

module;
#include "Core/Prelude.h"
#include "Core/Log/Log.h"
#include "Core/Reflection/Reflect.h"

export module draconic.script.facades;

import draconic.core;
import draconic.scene;
import draconic.script;

using namespace draconic::core;

export namespace draconic::script
{
    namespace dscene = draconic::scene;

    /// The service key the gameplay facades (Time/Random) resolve per context.
    inline constexpr StringView kScriptRuntimeService = u8"script.runtime";

    /// The ONE prelude line prepended to every (Wren) behavior module - the facade
    /// names resolve without user imports. Exactly one line: compile-error line
    /// numbers shift by one and reporters subtract it back.
    inline constexpr StringView kScriptBehaviorModulePrelude =
        u8"import \"main\" for Entity, Log, Time, Random, Scene\n";

    /// The OPTIONAL Wren `Behavior` base class (scripting.md §3.3 coroutines), injected
    /// into every Wren behaviors module right after the prelude. A behavior opts in with
    /// `class Mover is Behavior { construct new(entity) { super(entity) } ... }` to get
    /// `startCoroutine(fn)` / `wait(seconds)` / `waitUntil(fn)`; plain P1 classes that do
    /// NOT extend it are untouched. The base owns the instance's coroutine-id list and
    /// routes register/unregister to the backend's host-side scheduler through two
    /// foreign methods (bound by the Wren backend, independent of the reflected types).
    /// `wait` = `Fiber.yield(seconds)`; `waitUntil` polls in-script so the host scheduler
    /// only ever deals with numeric waits (no host-side predicate invocation).
    inline constexpr StringView kScriptWrenBehaviorBase =
        u8"class Behavior {\n"
        u8"  construct new(entity) {\n"
        u8"    _entity = entity\n"
        u8"    _drCoroutines = []\n"
        u8"  }\n"
        u8"  entity { _entity }\n"
        u8"  startCoroutine(fn) {\n"
        u8"    var fiber = Fiber.new(fn)\n"
        u8"    var w = fiber.call()\n"
        u8"    if (fiber.isDone) return -1\n"
        u8"    if (!(w is Num)) w = 0\n"
        u8"    var id = drRegisterCoroutine(fiber, w)\n"
        u8"    _drCoroutines.add(id)\n"
        u8"    return id\n"
        u8"  }\n"
        u8"  wait(seconds) { Fiber.yield(seconds) }\n"
        u8"  waitUntil(fn) {\n"
        u8"    while (!fn.call()) {\n"
        u8"      Fiber.yield(0)\n"
        u8"    }\n"
        u8"  }\n"
        u8"  foreign drRegisterCoroutine(fiber, w)\n"
        u8"  foreign drUnregisterCoroutine(id)\n"
        u8"  drCancelCoroutines() {\n"
        u8"    for (id in _drCoroutines) {\n"
        u8"      drUnregisterCoroutine(id)\n"
        u8"    }\n"
        u8"    _drCoroutines.clear()\n"
        u8"  }\n"
        u8"}\n";

    struct ScriptRuntimeBinding
    {
        f64 timeSeconds = 0.0;    // seconds since the run context was created
        f32 deltaSeconds = 0.0f;  // last frame's dt
        core::Random random;      // the run's RNG (per-run determinism seam)

        // Behavior-to-behavior messaging (P2): `entity.send("heal", amount)` routes here.
        // The subsystem installs this; it invokes `on<Heal>(amount)` on every behavior of
        // the target entity that declares the handler. Args are already marshalled. Null
        // when no subsystem is driving the run (a bare cook VM) - send becomes a no-op.
        core::Function<void(dscene::Scene*, dscene::EntityHandle, StringView,
                            core::Span<const core::Variant>)> dispatchMessage;

        // Prefab spawning (P2): `Scene.spawn(prefab, x, y, z)` routes here. The subsystem
        // sets `currentScene` around each scene's tick so the static facade knows WHERE to
        // spawn; the host app installs `spawnPrefab` (it owns the content DB that resolves
        // a prefab id to its payload). Null spawner (bare cook VM / no host) = safe no-op.
        dscene::Scene* currentScene = nullptr;
        core::Function<dscene::EntityHandle(dscene::Scene*, const core::Guid&,
                                            const core::Float3&)> spawnPrefab;
    };

    // ---- the curated behavior facades (camelCase = the script-visible names, the
    // Audio/Input facade precedent) ----

    /// The per-entity handle behaviors receive as their constructor argument: transform
    /// get/set, name, destroy. A value type - the VM instance carries a copy; a stale
    /// handle (entity destroyed) turns every call into a safe no-op.
    struct Entity
    {
        dscene::Scene* scene = nullptr;
        u32 entityIndex = dscene::EntityHandle::kInvalidIndex;
        u32 entityGeneration = 0;

        [[nodiscard]] dscene::EntityHandle Handle() const noexcept
        {
            return dscene::EntityHandle{ entityIndex, entityGeneration };
        }
        [[nodiscard]] bool Live() const noexcept
        {
            return scene != nullptr && scene->IsValid(Handle());
        }

        [[nodiscard]] bool isValid() const { return Live(); }
        [[nodiscard]] String name() const
        {
            return Live() ? String(scene->GetEntityName(Handle())) : String{};
        }
        void setName(String value)
        {
            if (Live()) { scene->SetEntityName(Handle(), value.AsView()); }
        }
        [[nodiscard]] Float3 position() const
        {
            return Live() ? scene->GetLocalTransform(Handle()).position
                          : Float3{ 0.0f, 0.0f, 0.0f };
        }
        void setPosition(f32 x, f32 y, f32 z)
        {
            if (Live()) { scene->SetLocalPosition(Handle(), Float3{ x, y, z }); }
        }
        [[nodiscard]] Float3 worldPosition() const
        {
            if (!Live()) { return Float3{ 0.0f, 0.0f, 0.0f }; }
            return TransformPoint(Float3{ 0.0f, 0.0f, 0.0f },
                                  scene->GetWorldMatrix(Handle()));
        }
        /// Absolute local rotation from Euler DEGREES (x = pitch, y = yaw, z = roll).
        void setRotationEuler(f32 xDegrees, f32 yDegrees, f32 zDegrees)
        {
            if (!Live()) { return; }
            Transform transform = scene->GetLocalTransform(Handle());
            transform.rotation = FromYawPitchRoll(DegreesToRadians(yDegrees),
                                                  DegreesToRadians(xDegrees),
                                                  DegreesToRadians(zDegrees));
            scene->SetLocalTransform(Handle(), transform);
        }
        void setScale(f32 x, f32 y, f32 z)
        {
            if (!Live()) { return; }
            Transform transform = scene->GetLocalTransform(Handle());
            transform.scale = Float3{ x, y, z };
            scene->SetLocalTransform(Handle(), transform);
        }
        void destroy()
        {
            if (Live()) { scene->DestroyEntity(Handle()); }
        }

        // ---- behavior messaging (P2 §3.4): entity.send(name[, arg]) invokes
        // `on<Name>(arg)` on EVERY behavior of this entity that declares it (the target
        // is this handle's entity - typically self or a resolved sibling). One typed arg
        // (number/string/entity) covers the common case; multi-arg/list is a follow-up.
        void send(String message) const { Dispatch(message.AsView(), {}); }
        void send(String message, f64 number) const
        {
            Variant arg = Variant::From<f64>(number);
            Dispatch(message.AsView(), Span<const Variant>{ &arg, 1 });
        }
        void send(String message, String text) const
        {
            Variant arg = Variant::From<String>(Move(text));
            Dispatch(message.AsView(), Span<const Variant>{ &arg, 1 });
        }
        void send(String message, Entity target) const
        {
            Variant arg = Variant::From<Entity>(target);
            Dispatch(message.AsView(), Span<const Variant>{ &arg, 1 });
        }

        void Dispatch(StringView message, Span<const Variant> args) const
        {
            if (!Live() || message.IsEmpty()) { return; }
            IScriptContext* context = CurrentScriptContext();
            auto* binding = context != nullptr
                ? static_cast<ScriptRuntimeBinding*>(context->GetService(kScriptRuntimeService))
                : nullptr;
            if (binding != nullptr && binding->dispatchMessage)
            {
                binding->dispatchMessage(scene, Handle(), message, args);
            }
        }
    };

    /// Log.info/warn/error -> the engine log, Script category (§6).
    class Log final : public Object
    {
        DRACONIC_OBJECT(Log, Object)
    public:
        static void info(String message)
        {
            DRACONIC_LOG_INFO(u8"Script", u8"{}", message);
        }
        static void warn(String message)
        {
            DRACONIC_LOG_WARNING(u8"Script", u8"{}", message);
        }
        static void error(String message)
        {
            DRACONIC_LOG_ERROR(u8"Script", u8"{}", message);
        }
    };

    /// Time.now() (seconds since the run started) / Time.delta() (last frame dt).
    class Time final : public Object
    {
        DRACONIC_OBJECT(Time, Object)
    public:
        [[nodiscard]] static ScriptRuntimeBinding* Resolve()
        {
            IScriptContext* context = CurrentScriptContext();
            return context != nullptr
                ? static_cast<ScriptRuntimeBinding*>(context->GetService(kScriptRuntimeService))
                : nullptr;
        }
        [[nodiscard]] static f64 now()
        {
            ScriptRuntimeBinding* binding = Resolve();
            return binding != nullptr ? binding->timeSeconds : 0.0;
        }
        [[nodiscard]] static f32 delta()
        {
            ScriptRuntimeBinding* binding = Resolve();
            return binding != nullptr ? binding->deltaSeconds : 0.0f;
        }
    };

    /// Random.value() in [0,1) / Random.range(min,max) / Random.intRange(min,max).
    class Random final : public Object
    {
        DRACONIC_OBJECT(Random, Object)
    public:
        [[nodiscard]] static f32 value()
        {
            ScriptRuntimeBinding* binding = Time::Resolve();
            return binding != nullptr ? binding->random.NextFloat() : 0.0f;
        }
        [[nodiscard]] static f32 range(f32 min, f32 max)
        {
            ScriptRuntimeBinding* binding = Time::Resolve();
            return binding != nullptr ? binding->random.NextFloat(min, max) : min;
        }
        [[nodiscard]] static i32 intRange(i32 min, i32 max)
        {
            ScriptRuntimeBinding* binding = Time::Resolve();
            return (binding != nullptr && max >= min) ? binding->random.NextInt(min, max) : min;
        }
    };

    /// Scene.spawn(prefab, x, y, z): instantiates a prefab into the CURRENT scene at a
    /// world position, returning the spawned root's Entity handle (invalid if no spawner
    /// is wired or the prefab id is nil). The prefab id comes from an `asset:Prefab`
    /// behavior property (marshalled as a Guid).
    class Scene final : public Object
    {
        DRACONIC_OBJECT(Scene, Object)
    public:
        [[nodiscard]] static ScriptRuntimeBinding* Resolve()
        {
            IScriptContext* context = CurrentScriptContext();
            return context != nullptr
                ? static_cast<ScriptRuntimeBinding*>(context->GetService(kScriptRuntimeService))
                : nullptr;
        }

        [[nodiscard]] static Entity Wrap(dscene::Scene* scene, dscene::EntityHandle handle)
        {
            Entity result;
            if (scene != nullptr && handle.IsAssigned())
            {
                result.scene = scene;
                result.entityIndex = handle.index;
                result.entityGeneration = handle.generation;
            }
            return result;
        }

        [[nodiscard]] static Entity spawn(Guid prefab, f32 x, f32 y, f32 z)
        {
            ScriptRuntimeBinding* binding = Resolve();
            if (binding == nullptr || binding->currentScene == nullptr
                || !binding->spawnPrefab || prefab.IsNil())
            {
                return Entity{};
            }
            return Wrap(binding->currentScene,
                        binding->spawnPrefab(binding->currentScene, prefab, Float3{ x, y, z }));
        }

        /// First entity in the current scene with this name (invalid if none).
        [[nodiscard]] static Entity find(String name)
        {
            ScriptRuntimeBinding* binding = Resolve();
            return (binding != nullptr && binding->currentScene != nullptr)
                ? Wrap(binding->currentScene, binding->currentScene->FindEntityByName(name.AsView()))
                : Entity{};
        }

        /// Resolve a '/'-separated hierarchy path from the current scene's roots, e.g.
        /// "Player/Weapon/Muzzle" (invalid if any segment misses).
        [[nodiscard]] static Entity findByPath(String path)
        {
            ScriptRuntimeBinding* binding = Resolve();
            return (binding != nullptr && binding->currentScene != nullptr)
                ? Wrap(binding->currentScene, binding->currentScene->FindEntityByPath(path.AsView()))
                : Entity{};
        }
    };

    /// Registers the behavior facade types (Entity/Log/Time/Random/Scene) with the global
    /// registry - call BEFORE a script manager is created (the run host and the cook's
    /// builder both do). Idempotent.
    void RegisterScriptFacadeReflection();
}
