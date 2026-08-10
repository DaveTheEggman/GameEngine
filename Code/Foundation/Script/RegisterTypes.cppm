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
    // FAMILY - every backend dispatches it soundly by argument COUNT (Wren signatures encode arity,
    // AS overloads natively, the Luau thunk switches on argc), and typed decls express it as an
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
