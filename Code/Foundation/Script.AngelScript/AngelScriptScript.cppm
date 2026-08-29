// Foundation::Script.AngelScript - AngelScript backend (foundation.script.angelscript).
//
// Implements Foundation::Script.AngelScript on AngelScript (the second certified backend - the
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
//  - Overloads register per exact signature (no arity-collapsing).
//  - Scalars map f32->float, f64->double, i32->int, u32->uint, i64->int64,
//    u64->uint64 (+8/16-bit widths), bool->bool, core String<->script `string`.
//
// This is a module INTERFACE unit: engine types only. ALL AngelScript SDK
// contact (angelscript.h, scriptstdstring) lives in AngelScriptScriptImpl.cpp
// (GCC module hygiene - heavy third-party headers never sit in an interface
// unit's global module fragment).

module;
#include "Core/Prelude.h"

export module foundation.script.angelscript;

import foundation.core;
import foundation.script;

namespace core = foundation::core;

export namespace foundation::script::angelscript
{
    [[nodiscard]] core::RefPtr<IScriptManager> CreateScriptManager();

    /// Registers AngelScript with the backend registry - the ONE
    /// line that makes the language available; consumers resolve by extension
    /// (u8"as") or language id (u8"angelscript"), never by backend type. A game/project
    /// opts in by calling this from its entry point (exactly like registering extra
    /// subsystems).
    void RegisterAngelScriptBackend();

    /// Editor-cook seam: the raw `asIScriptEngine*` (as an opaque `void*`) behind a
    /// manager THIS backend created, so the AngelScript editor cook can drive the
    /// CScriptBuilder add-on for `[metadata]` property harvest against an engine that
    /// already has the reflected types registered. Returns null for a null/foreign
    /// manager. `void*` keeps the AngelScript SDK header out of this interface unit
    /// (GCC module hygiene). Runtime dispatch never uses this - it is a tooling hook.
    [[nodiscard]] void* AngelScriptEngineHandle(IScriptManager& manager) noexcept;

    /// The in-module coroutine support section (`Coroutine::waitUntil`) the runtime
    /// adds to every loaded behavior module. Exposed so the editor cook, which builds
    /// the behavior through CScriptBuilder, compiles it with the SAME surface the
    /// runtime does (a coroutine-using behavior harvests exactly as it runs).
    [[nodiscard]] core::StringView AngelScriptCoroutineModulePrelude() noexcept;

    /// The AngelScript bytecode-format version (ANGELSCRIPT_VERSION). Bytecode is tied to the
    /// library version, so the cook folds this into its fingerprint: an AngelScript vendor bump
    /// changes the number and every AngelScript script pack recooks (mirrors LuauBytecodeVersion).
    /// Kept out-of-line so the SDK header stays in the impl unit (GCC module hygiene).
    [[nodiscard]] core::u32 AngelScriptBytecodeVersion() noexcept;

    /// Wrap an already-built module's bytecode into a serializable IScriptBlob (SaveByteCode +
    /// the same blob type CompileToBlob/LoadBlob use). The AngelScript cook builds through
    /// CScriptBuilder (to strip `[metadata]`), so it must SaveByteCode the module it ALREADY
    /// built rather than recompile the raw source; this exposes that on the neutral blob seam so
    /// the cook stores + the runtime reconstructs bytecode identically to Luau. `module` is an
    /// `asIScriptModule*` as `void*` (SDK header stays out of the cook). Null on failure.
    [[nodiscard]] core::RefPtr<IScriptBlob> AngelScriptBlobFromModule(void* module);
}
