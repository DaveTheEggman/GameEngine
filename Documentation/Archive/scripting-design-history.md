# Scripting - design history (archived)

> Status: ARCHIVED
> Superseded by: Documentation/Systems/scripting.md (present-tense reference)
> Track: [[script-behaviors-p1]]

NON-AUTHORITATIVE historical record. The scripting subsystem's design rationale from the
2026-07 planning + survey era. Present-tense truth lives in `Systems/scripting.md`; the full
original design doc (all sections, verbatim) is in git history at commit 3b92560d
(`Documentation/Systems/scripting.md` before the P1 rewrite). Kept here for the "why".

## Reference survey - conclusions (2026-07-17)

Surveyed Lumix Lua, Godot ScriptInstance, ez AngelScript before designing entity behaviors:

- **Lumix** (`src/lua`): multiple scripts per entity in ONE component (reorderable array);
  per-instance VM env with `this`; lifecycle by named lookup; properties discovered from the
  instance and stored as name-HASHED override blobs re-applied after hot reload (rename-safe,
  editor values survive); deferred start queue; subsystems call INTO scripts through one
  generic `beginFunctionCall`. Awkward: global update arrays = no deterministic cross-entity
  order; any stray global becomes an inspector field.
- **Godot** (`core/object/script_language.h`): the clean triad - Script (asset) /
  ScriptInstance (per-object binding: set/get/callp/property-list, Variant + PropertyInfo) /
  ScriptLanguage (VM). Exports are ordinary object properties -> scenes serialize only values
  differing from the script default (diff + inspector revert free). Placeholder instances keep
  property values alive across compile failures/reloads. Awkward: one script per object; huge
  language interface.
- **ez** (`Core/Scripting` + AngelScriptPlugin): script class derives the reflected component
  base -> reuses the reflection/message/serialization stack; exposed parameters harvested from
  the ASSET at cook (data-driven inspector, only overrides serialize); collision/trigger events
  arrive as ordinary MESSAGES; coroutine scheduler with Wait/MoveTo/TweenProperty; per-component
  update interval + only-when-simulating gating. Reload = full re-instantiate + re-apply params.

**Adopted:** Lumix's array-of-behaviors-in-one-component + hashed override blobs; ez's
cook-time harvest + simulation gating + coroutines; Godot's asset/instance split (we already
have resource + `ScriptObject`) and default-diff property semantics.

## Resolved open questions (2026-07-17)

1. `static properties` map vs annotation comments? -> the static map (real code; the cook VM
   exists anyway; Wren has no annotation syntax). Per-language equivalents shipped.
2. One shared gameplay context vs context-per-scene? -> ONE per RUN (matches the PIE teardown
   rule); scene unload destroys that scene's instances but keeps the context.
3. Does the game script get the behavior facades (`Scene.spawn` etc.)? -> Yes; same context,
   same modules; tiers differ only in lifecycle, not capability.

## Addendum (2026-07-18): host-object injection + the Input facade

Wren's C API has no `wrenSetVariable` (get/has only), so `WrenScriptContext::SetGlobal` could
not inject host objects as module globals. Resolved via **per-context host services**: the
backend stores its context in VM user data, every foreign shim fetches it, and
`context->SetService(type, ptr)` routes facade statics through it - each VM resolves its own
runtime, script-side API unchanged. (This is now the general per-context service mechanism used
by every subsystem facade.)

## Traktor findings (re-read 2026-07-19, partly inspired us)

Their `IScriptManager` is Lua-backed behind the same shape (registrar + createContext + GC
stepping). Three seams adopted as phases arrived: `IScriptCompiler`->`IScriptBlob` (compile at
cook, ship bytecode - now the Bytecode capability, shipped for Luau + AngelScript);
`IScriptDebugger`/`IScriptProfiler` (capability flags; debuggers shipped for AS + Luau); and
incremental `collectGarbage(full)` stepping for frame-budgeted GC (step at high frequency to
keep the heap small rather than paying full collections mid-game).
