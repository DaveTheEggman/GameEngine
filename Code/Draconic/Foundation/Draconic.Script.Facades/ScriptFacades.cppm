// Draconic::ScriptFacades - the `draconic.script.facades` module.
//
// The curated behavior facades (docs/design/scripting.md §3.5): the per-entity
// `Entity` handle behaviors receive as their constructor argument, plus Log/Time/
// Random. A SEPARATE library (below the subsystem) so the COOK's harvest VM and the
// RUNTIME register the SAME "main"-module surface - a facade-using behavior that
// compiles at cook compiles at runtime and vice versa.
//
// Module-visibility rule (language-specific, so it lives in the BACKENDS): a backend
// whose reflected classes live in a separate module frames each behavior module with a
// prelude that imports the facade names. To keep that framing OUT of this neutral lib,
// the facade name list is exposed here as data (BehaviorFacadeNames) and each backend's
// AssembleBehaviorModuleSource builds its own prelude from it - so adding a facade never
// edits a backend.

module;
#include "Draconic.Core/Prelude.h"
#include "Draconic.Core/Log/Log.h"
#include "Draconic.Core/Reflection/Reflect.h"

export module draconic.script.facades;

import draconic.core;
import draconic.scene;
import draconic.script;

using namespace draconic::core;

export namespace draconic::script
{
    namespace scene = draconic::scene;

    /// The service key the gameplay facades (Time/Random) resolve per context.
    inline constexpr StringView kScriptRuntimeService = u8"script.runtime";

    /// The reflected behavior-facade names a backend's behavior-module prelude must make
    /// visible (the classes RegisterScriptFacadeReflection installs). Exposed as DATA so
    /// the language framing stays in the backends: a Wren backend builds its import line
    /// from this list, and adding a facade here reaches every backend for free. Order is
    /// the authored order. Kept in sync with RegisterScriptFacadeReflection below.
    [[nodiscard]] core::Span<const core::StringView> BehaviorFacadeNames();

    /// Register an ADDITIONAL facade name from an out-of-tree module (e.g. draconic.net's `Net`),
    /// so the behavior prelude imports it too - without this base lib depending on that module.
    /// Idempotent; call alongside the module's own reflection registration. Names are borrowed as
    /// interned copies. See ExtraFacadeNames.
    void RegisterExtraFacadeName(core::StringView name);
    /// The extra facade names registered by other modules (appended to the prelude after the built-ins).
    [[nodiscard]] core::Span<const core::StringView> ExtraFacadeNames();

    /// Register an ADDITIONAL emission root: a reflected type the Wren collector should emit as a
    /// script class even when no facade signature statically reaches it (e.g. a component type
    /// returned only via a factory whose declared return IS that type - `RigidBody.of(entity)`).
    /// GENERAL, not component-specific (the UI View reflection follow-on wants the same door).
    /// AngelScript already emits every registry type, so this is consumed by the Wren collector.
    /// Idempotent; the type is borrowed (its TypeInfo has static lifetime).
    void RegisterExtraScriptRootType(const core::TypeInfo* type);
    /// The additional emission roots registered by other modules (extra seeds for the Wren closure).
    [[nodiscard]] core::Span<const core::TypeInfo* const> ExtraScriptRootTypes();

    struct ScriptRuntimeBinding
    {
        f64 timeSeconds = 0.0;   // seconds since the run context was created
        f32 deltaSeconds = 0.0f; // last frame's dt
        core::Random random;     // the run's RNG (per-run determinism seam)

        // Behavior-to-behavior messaging (P2): `entity.send("heal", amount)` routes here.
        // The subsystem installs this; it invokes `on<Heal>(amount)` on every behavior of
        // the target entity that declares the handler. Args are already marshalled. Null
        // when no subsystem is driving the run (a bare cook VM) - send becomes a no-op.
        core::Function<void(scene::Scene*, scene::EntityHandle, StringView,
                            core::Span<const core::Variant>)>
            dispatchMessage;

        // Prefab spawning (P2): `scene.spawn(prefab, x, y, z)` on a BOUND Scene routes here. The
        // host app installs `spawnPrefab` (it owns the content DB that resolves a prefab id to its
        // payload); the bound Scene passes its OWN scene ptr, so there is no ambient current-scene
        // state to keep correct. Null spawner (bare cook VM / no host) = safe no-op.
        core::Function<scene::EntityHandle(scene::Scene*, const core::Guid&, const core::Float3&)>
            spawnPrefab;
    };

    // ---- the curated behavior facades (camelCase = the script-visible names, the
    // Audio/Input facade precedent) ----

    struct Scene; // bound scene handle (defined below); Entity.scene returns one

    /// The per-entity handle behaviors receive as their constructor argument: transform
    /// get/set, name, destroy. A value type - the VM instance carries a copy; a stale
    /// handle (entity destroyed) turns every call into a safe no-op.
    struct Entity
    {
        scene::Scene* scene = nullptr;
        u32 entityIndex = scene::EntityHandle::kInvalidIndex;
        u32 entityGeneration = 0;

        [[nodiscard]] scene::EntityHandle Handle() const noexcept
        {
            return scene::EntityHandle{entityIndex, entityGeneration};
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
            if (Live())
            {
                scene->SetEntityName(Handle(), value.AsView());
            }
        }
        [[nodiscard]] Float3 position() const
        {
            return Live() ? scene->GetLocalTransform(Handle()).position : Float3{0.0f, 0.0f, 0.0f};
        }
        void setPosition(f32 x, f32 y, f32 z)
        {
            if (Live())
            {
                scene->SetLocalPosition(Handle(), Float3{x, y, z});
            }
        }
        [[nodiscard]] Float3 worldPosition() const
        {
            if (!Live())
            {
                return Float3{0.0f, 0.0f, 0.0f};
            }
            return TransformPoint(Float3{0.0f, 0.0f, 0.0f}, scene->GetWorldMatrix(Handle()));
        }
        /// Absolute local rotation from Euler DEGREES (x = pitch, y = yaw, z = roll).
        void setRotationEuler(f32 xDegrees, f32 yDegrees, f32 zDegrees)
        {
            if (!Live())
            {
                return;
            }
            Transform transform = scene->GetLocalTransform(Handle());
            transform.rotation = FromYawPitchRoll(
                DegreesToRadians(yDegrees), DegreesToRadians(xDegrees), DegreesToRadians(zDegrees));
            scene->SetLocalTransform(Handle(), transform);
        }
        void setScale(f32 x, f32 y, f32 z)
        {
            if (!Live())
            {
                return;
            }
            Transform transform = scene->GetLocalTransform(Handle());
            transform.scale = Float3{x, y, z};
            scene->SetLocalTransform(Handle(), transform);
        }
        void destroy()
        {
            if (Live())
            {
                scene->DestroyEntity(Handle());
            }
        }

        /// The BOUND scene this entity belongs to (reflected as `.scene`). Operating through it
        /// (`entity.scene.spawn/find/...`) always targets THIS entity's scene - correct from any
        /// call site (update, onDestroy, a physics event, a stored callback), no ambient state.
        /// Named `sceneHandle()` in C++ to avoid clashing with the `scene` data member; the script
        /// name is `scene`. Defined out-of-line (Scene is completed below).
        [[nodiscard]] Scene sceneHandle() const;

        // ---- behavior messaging (P2 §3.4): entity.send(name[, arg]) invokes
        // `on<Name>(arg)` on EVERY behavior of this entity that declares it (the target
        // is this handle's entity - typically self or a resolved sibling). One typed arg
        // (number/string/entity) covers the common case; multi-arg/list is a follow-up.
        void send(String message) const { Dispatch(message.AsView(), {}); }
        void send(String message, f64 number) const
        {
            Variant arg = Variant::From<f64>(number);
            Dispatch(message.AsView(), Span<const Variant>{&arg, 1});
        }
        void send(String message, String text) const
        {
            Variant arg = Variant::From<String>(Move(text));
            Dispatch(message.AsView(), Span<const Variant>{&arg, 1});
        }
        void send(String message, Entity target) const
        {
            Variant arg = Variant::From<Entity>(target);
            Dispatch(message.AsView(), Span<const Variant>{&arg, 1});
        }

        void Dispatch(StringView message, Span<const Variant> args) const
        {
            if (!Live() || message.IsEmpty())
            {
                return;
            }
            IScriptContext* context = CurrentScriptContext();
            auto* binding =
                context != nullptr
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
        static void info(String message) { DRACONIC_LOG_INFO(u8"Script", u8"{}", message); }
        static void warn(String message) { DRACONIC_LOG_WARNING(u8"Script", u8"{}", message); }
        static void error(String message) { DRACONIC_LOG_ERROR(u8"Script", u8"{}", message); }
    };

    /// Time.now() (seconds since the run started) / Time.delta() (last frame dt).
    class Time final : public Object
    {
        DRACONIC_OBJECT(Time, Object)
    public:
        [[nodiscard]] static ScriptRuntimeBinding* Resolve()
        {
            IScriptContext* context = CurrentScriptContext();
            return context != nullptr ? static_cast<ScriptRuntimeBinding*>(
                                            context->GetService(kScriptRuntimeService))
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

    /// A BOUND scene handle. `scene.spawn(prefab,x,y,z)` / `scene.find(name)` /
    /// `scene.findByPath(path)` all operate on THIS scene (the ptr the value carries), so there is
    /// no ambient "current scene" to keep correct - a call from any site (update, onDestroy, a
    /// physics contact, a resumed coroutine, a stored callback) always targets the right scene.
    /// Behaviors reach it via `entity.scene`; the Level tier receives one as its constructor
    /// argument; the orchestrator queries `SceneLoader.currentScene()`. A value type (the VM carries
    /// a copy); a null/stale scene makes every call a safe no-op returning an invalid Entity.
    struct Scene
    {
        scene::Scene* scene = nullptr;

        /// Instantiate a prefab into THIS scene at a world position; invalid Entity if the scene is
        /// null, no spawner is wired, or the prefab id is nil. The id comes from an `asset:Prefab`
        /// behavior property (marshalled as a Guid).
        [[nodiscard]] Entity spawn(Guid prefab, f32 x, f32 y, f32 z) const;
        /// First entity in THIS scene with this name (invalid if none).
        [[nodiscard]] Entity find(String name) const;
        /// Resolve a '/'-separated hierarchy path from THIS scene's roots, e.g.
        /// "Player/Weapon/Muzzle" (invalid if any segment misses).
        [[nodiscard]] Entity findByPath(String path) const;
    };

    /// Wrap a (scene, handle) pair into an Entity value (invalid handle -> invalid Entity).
    [[nodiscard]] inline Entity WrapEntity(scene::Scene* scene, scene::EntityHandle handle)
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

    inline Scene Entity::sceneHandle() const
    {
        Scene handle;
        handle.scene = scene; // the entity's own scene - never ambient
        return handle;
    }

    inline Entity Scene::spawn(Guid prefab, f32 x, f32 y, f32 z) const
    {
        if (scene == nullptr || prefab.IsNil())
        {
            return Entity{};
        }
        IScriptContext* context = CurrentScriptContext();
        auto* binding =
            context != nullptr
                ? static_cast<ScriptRuntimeBinding*>(context->GetService(kScriptRuntimeService))
                : nullptr;
        if (binding == nullptr || !binding->spawnPrefab)
        {
            return Entity{};
        }
        return WrapEntity(scene, binding->spawnPrefab(scene, prefab, Float3{x, y, z}));
    }

    inline Entity Scene::find(String name) const
    {
        return (scene != nullptr) ? WrapEntity(scene, scene->FindEntityByName(name.AsView()))
                                  : Entity{};
    }

    inline Entity Scene::findByPath(String path) const
    {
        return (scene != nullptr) ? WrapEntity(scene, scene->FindEntityByPath(path.AsView()))
                                  : Entity{};
    }

    /// Registers the behavior facade types (Entity/Log/Time/Random/Scene) with the
    /// registry - call BEFORE a script manager is created (the run host and the cook's
    /// builder both do). Idempotent.
    void RegisterScriptFacadeReflection();
}
