// Script - :script_register partition
//
// Bridges reflection -> scripting: registers every type in a TypeRegistry with a
// script manager. Call after the engine's RegisterCoreTypes() (and any
// higher-layer registration) to expose them to scripts in one shot.

module;
#include "Core/Prelude.h"
#include "Core/Log/Log.h"
#include "Core/Debug/Assert.h"

export module foundation.script:script_register;

import foundation.core;
import :script_manager;

namespace core = foundation::core;

export namespace foundation::script
{
    // The name a method is spelled by on the SCRIPT surface: its reflected overloadedName when set,
    // else its C++ name. This is the scripting layer's INTERPRETATION of the opaque overloadedName
    // metadata Core stores - every backend emits/binds by THIS, so an overload's spelling is
    // identical across languages (the backend-neutral surface script_api + agents consume).
    [[nodiscard]] inline const char* ScriptMethodName(const core::MethodInfo& method) noexcept
    {
        return (method.overloadedName != nullptr && method.overloadedName[0] != '\0')
                   ? method.overloadedName
                   : method.name;
    }

    // The overload contract keys on the SCRIPT-METHOD IDENTITY triple (name, ARITY, staticness):
    // no two methods on a type may share all three. Same-name-DIFFERENT-arity is a legal ARITY
    // FAMILY - every backend dispatches it soundly by argument COUNT (AS overloads natively, the
    // Luau thunk switches on argc), and typed decls express it as an
    // overloaded function type. Only a same-name SAME-arity clash is ambiguous on a dynamically-
    // typed surface (a number picks neither f32 nor i32) and MUST be split by distinct
    // OverloadedName()s. Returns the first colliding script name, or nullptr when the type is clean.
    [[nodiscard]] inline const char*
    FindScriptMethodNameCollision(const core::TypeInfo& type) noexcept
    {
        const auto equal = [](const char* a, const char* b) noexcept
        {
            if (a == b)
            {
                return true;
            }
            if (a == nullptr || b == nullptr)
            {
                return false;
            }
            while (*a != '\0' && *a == *b)
            {
                ++a;
                ++b;
            }
            return *a == *b;
        };
        const core::Span<const core::MethodInfo> methods = core::Methods(type);
        for (core::usize i = 0; i < methods.Size(); ++i)
        {
            const char* nameI = ScriptMethodName(methods[i]);
            for (core::usize j = i + 1; j < methods.Size(); ++j)
            {
                // Collision = same (name, arity, static). Different arity is a legal family.
                if (methods[i].isStatic == methods[j].isStatic &&
                    methods[i].paramCount == methods[j].paramCount &&
                    equal(nameI, ScriptMethodName(methods[j])))
                {
                    return nameI;
                }
            }
        }
        return nullptr;
    }

    // FinalizeTypes calls this over the types a backend has registered: it FAILS LOUDLY (logs the
    // offending type + name, then traps) on the first overload that lacks distinct overloadedNames.
    // This converts a silent one-overload-wins collision into an immediate registration-time error
    // on EVERY backend - the contract that makes overloadedName enforceable, not advisory.
    inline void ValidateScriptMethodNames(core::Span<const core::TypeInfo* const> types)
    {
        for (const core::TypeInfo* type : types)
        {
            if (type == nullptr)
            {
                continue;
            }
            const char* collision = FindScriptMethodNameCollision(*type);
            if (collision != nullptr)
            {
                LOG_ERROR(u8"Script",
                          u8"reflected type '{}' binds two methods to the script name '{}' - give "
                          u8"each overload a distinct .OverloadedName(...)",
                          core::StringView(reinterpret_cast<const core::utf8char*>(type->name)),
                          core::StringView(reinterpret_cast<const core::utf8char*>(collision)));
                DIAGNOSTIC_ASSERT(collision == nullptr &&
                                  "overloaded methods need distinct overloadedName for scripting");
            }
        }
    }

    // The name a TYPE is spelled by on the SCRIPT surface: its "scriptName" class-attribute alias when
    // set, else its C++ name. This is the scripting layer's INTERPRETATION of an opaque Core attribute
    // (Core knows nothing of "scriptName" - it stores a generic key/value like the editor's displayName);
    // every backend binds the class under THIS. Lets a C++ type expose a script-idiomatic name (`run` for
    // class Run, later `RigidBody` for RigidBodyComponent) without touching the wire/native identity -
    // serialization + TypeRegistry FindByName stay on the C++ `name`, which never aliases.
    inline constexpr const char* kScriptNameAttribute = "scriptName";

    [[nodiscard]] inline const char* ScriptTypeName(const core::TypeInfo& type) noexcept
    {
        const core::Variant* alias = core::FindAttribute(type, kScriptNameAttribute);
        if (alias != nullptr && alias->Is<const char*>())
        {
            const char* aliasName = alias->Get<const char*>();
            if (aliasName != nullptr && aliasName[0] != '\0')
            {
                return aliasName;
            }
        }
        return type.name;
    }

    // A duplicate SCRIPT-FACING type name (a C++ class name OR a scriptName alias) shared by two types
    // is illegal - two types cannot both bind as `X` (a silent last-wins would shadow one, and it is how
    // a facade claiming a reserved name like `run` collides with anything else claiming it). Returns the
    // first colliding script name, or nullptr when the set is clean. The type-level analog of
    // FindScriptMethodNameCollision.
    [[nodiscard]] inline const char*
    FindScriptTypeNameCollision(core::Span<const core::TypeInfo* const> types) noexcept
    {
        const auto equal = [](const char* a, const char* b) noexcept
        {
            if (a == b)
            {
                return true;
            }
            if (a == nullptr || b == nullptr)
            {
                return false;
            }
            while (*a != '\0' && *a == *b)
            {
                ++a;
                ++b;
            }
            return *a == *b;
        };
        for (core::usize i = 0; i < types.Size(); ++i)
        {
            if (types[i] == nullptr)
            {
                continue;
            }
            const char* nameI = ScriptTypeName(*types[i]);
            for (core::usize j = i + 1; j < types.Size(); ++j)
            {
                if (types[j] != nullptr && equal(nameI, ScriptTypeName(*types[j])))
                {
                    return nameI;
                }
            }
        }
        return nullptr;
    }

    // FinalizeTypes calls this over the types a backend has registered: it FAILS LOUDLY on the first
    // duplicate script-facing type name. Extends the overloaded-name-contract's loud-collision rule to
    // the TYPE level - the alias mechanism's guard.
    inline void ValidateScriptTypeNames(core::Span<const core::TypeInfo* const> types)
    {
        const char* collision = FindScriptTypeNameCollision(types);
        if (collision != nullptr)
        {
            LOG_ERROR(u8"Script",
                      u8"two reflected types bind to the same script name '{}' (a class name or a "
                      u8"scriptName alias) - names must be unique across the bound surface",
                      core::StringView(reinterpret_cast<const core::utf8char*>(collision)));
            DIAGNOSTIC_ASSERT(collision == nullptr &&
                              "duplicate script-facing type name (class name or scriptName)");
        }
    }

    inline void
    RegisterReflectedTypes(IScriptManager& manager,
                           const core::TypeRegistry& registry = core::GlobalTypeRegistry())
    {
        for (const core::TypeInfo* type : registry.All())
        {
            manager.RegisterType(*type);
        }
        manager.FinalizeTypes(); // two-phase backends emit here (declare-all, then bind)
    }
}
