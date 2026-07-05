// Draconic::Script - the `draconic.script` module.
//
// A thin, VM-agnostic scripting abstraction built on Core's reflection: an
// IScriptManager (the VM) creates IScriptContexts, reflected types are
// registered with it, and all values cross the boundary as Variant. Concrete
// VM backends (Lua, ...) are plugins implementing these interfaces.

export module draconic.script;

export import :script_context;
export import :script_manager;
export import :script_register;
