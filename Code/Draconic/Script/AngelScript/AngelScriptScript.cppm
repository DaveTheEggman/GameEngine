// Draconic::ScriptAngelScript - AngelScript backend (draconic.script.angelscript).
//
// Implements Draconic::Script on AngelScript (the second certified backend - the
// proof that the contract is backend-agnostic). Reflected types are emitted with
// the TWO-PHASE flow the contract exists for (IScriptManager::FinalizeTypes):
// phase 1 declares every collected type via RegisterObjectType, THEN phase 2
// registers behaviours/properties/methods - so declaration strings may reference
// any reflected type regardless of registration order.
//
// The emission mapping (how reflected types appear in AngelScript syntax):
//  - Constructible value/Object types -> reference types (asOBJ_REF) whose
//    instances box the engine Variant; each reflected constructor becomes a
//    factory: `Float3(1, 2, 3)`.
//  - Properties -> virtual property accessors: `v.x` / `v.x = 9`.
//  - Instance methods -> object methods: `box.Contains(p)`.
//  - Static methods -> global functions in a NAMESPACE named after the class
//    (AngelScript's documented stand-in for statics): `Float3::Dot(a, b)`.
//  - Overloads register per exact signature (no arity-collapsing like Wren).
//  - Scalars map f32->float, f64->double, i32->int, u32->uint, i64->int64,
//    u64->uint64 (+8/16-bit widths), bool->bool, core String<->script `string`.
//
// This is a module INTERFACE unit: engine types only. ALL AngelScript SDK
// contact (angelscript.h, scriptstdstring) lives in AngelScriptScriptImpl.cpp
// (GCC module hygiene - heavy third-party headers never sit in an interface
// unit's global module fragment).

module;
#include "Core/Prelude.h"

export module draconic.script.angelscript;

import draconic.core;
import draconic.script;

namespace core = draconic::core;

export namespace draconic::script::angelscript
{
    [[nodiscard]] core::RefPtr<IScriptManager> CreateScriptManager();

    /// Registers AngelScript with the backend registry (scripting.md B1) - the ONE
    /// line that makes the language available; consumers resolve by extension
    /// (u8"as") or language id (u8"angelscript"), never by backend type. Wren stays
    /// the batteries-included default: a game/project opts in by calling this from
    /// its entry point (exactly like registering extra subsystems).
    void RegisterAngelScriptBackend();
}
