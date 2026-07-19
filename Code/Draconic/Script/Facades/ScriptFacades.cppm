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
        u8"import \"main\" for Entity, Log, Time, Random\n";

    struct ScriptRuntimeBinding
    {
        f64 timeSeconds = 0.0;    // seconds since the run context was created
        f32 deltaSeconds = 0.0f;  // last frame's dt
        core::Random random;      // the run's RNG (per-run determinism seam)
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

    /// Registers the behavior facade types (Entity/Log/Time/Random) with the global
    /// registry - call BEFORE a script manager is created (the run host and the cook's
    /// builder both do). Idempotent.
    void RegisterScriptFacadeReflection();
}
