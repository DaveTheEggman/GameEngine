// Draconic::ScriptAngelScriptEditor - the `draconic.script.angelscript.editor` module.
//
// The AngelScript cook service (scripting.md §5 + §7.5): compile-check in a cooker-owned
// AngelScript VM (resolved through the backend registry by language) + the shared
// on<Upper>(...) handler scan + PROPERTY HARVEST + an AngelScript starter template.
//
// AngelScript's editor-property surface is typed member FIELDS annotated with the
// language's own `[metadata]`: `[default, "description"]` before a field declares that
// field an inspector property (a field with no metadata is not one). The cook builds the
// behavior through the vendored CScriptBuilder add-on (which pre-processes `[metadata]`)
// and walks the class's fields, mapping each metadata'd field's declared type + default +
// description into the SAME ScriptPropertyDesc metadata the Wren cook produces. All the
// AngelScript / scriptbuilder contact lives in the implementation unit.
//
// Plain module interface unit: no AngelScript SDK header appears here (GCC module hygiene
// by construction); the implementation unit reaches the cook VM's engine through the
// backend's AngelScriptEngineHandle seam.

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
