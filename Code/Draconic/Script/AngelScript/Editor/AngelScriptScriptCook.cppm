// Draconic::ScriptAngelScriptEditor - the `draconic.script.angelscript.editor` module.
//
// The AngelScript cook service (scripting.md §5 + §7.5): a MINIMAL cook - compile-check
// in a cooker-owned AngelScript VM (resolved through the backend registry by language) +
// the shared on<Upper>(...) handler scan + an AngelScript starter template.
//
// PROPERTY HARVEST IS DEFERRED for AngelScript. AngelScript has no `static properties`
// map like Wren's, and its property convention is a separate design task; so an AS
// behavior cooks compile-checked with text-scanned handlers and NO property metadata,
// exactly as scripting.md §7.5 anticipates ("other languages cook compile-checked with
// text-scanned handlers and no property metadata until they grow their own probe"). When
// AS grows a property surface, only this cook changes - the neutral pipeline is untouched.
//
// Plain module interface unit: the cook VM is reached through the neutral IScriptContext
// surface, so no AngelScript SDK header appears here (GCC module hygiene by construction).

module;
#include "Core/Prelude.h"

export module draconic.script.angelscript.editor;

import draconic.core;

using namespace draconic::core;

export namespace draconic::script
{
    // The New Asset starter for AngelScript: a behavior class whose constructor takes the
    // entity handle, with the lifecycle handlers stubbed. No properties (harvest deferred).
    inline constexpr StringView kAngelScriptBehaviorStarter =
        u8"// Behavior class - attach via a ScriptComponent behavior slot.\n"
        u8"// Reflected engine types (Entity, Float3, Log, Time, Random, Scene) are visible\n"
        u8"// globally - no import needed.\n"
        u8"class NewBehavior\n"
        u8"{\n"
        u8"    private Entity@ self;\n"
        u8"\n"
        u8"    NewBehavior(Entity@ entity) { @self = entity; }\n"
        u8"\n"
        u8"    void onStart() {}\n"
        u8"    void onUpdate(double dt) {}\n"
        u8"    void onDestroy() {}\n"
        u8"}\n";

    /// Registers the AngelScript cook (and, idempotently, the AngelScript backend it
    /// needs) so the neutral ScriptClassAssetBuilder resolves it by language. Entry points
    /// call this alongside RegisterWrenScriptCook(). Idempotent.
    void RegisterAngelScriptScriptCook();
}
