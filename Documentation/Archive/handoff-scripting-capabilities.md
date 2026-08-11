# Handoff: scripting capabilities slice (delegates + introspection + seam interfaces)

Adds four capabilities to the backend-neutral scripting contract. Two are implemented in
full on both backends and certified; three are laid down as interface + capability flag +
conformance skeleton, declared **absent** for now (committed seams, not faked-working).
Design confirmed with the user. Run on a NEW branch `script-capabilities` **only after
the `angelscript-harvest` and `scriptpage` branches are merged to master** (this touches
`IScriptManager` + both runtime backends — collides with those).

Contract lives in `Code/Foundation/Script/` (`IScriptManager.cppm`, `IScriptContext.cppm`,
`Variant`, `ScriptCapabilities`, the conformance battery `Tests/BackendConformance.h`).
Backends: `Wren/WrenScript.cppm`, `AngelScript/AngelScriptScriptImpl.cpp`. Reflection
marshalling: `Core/Reflection/` (`ConvertArg`/`AcceptArg`/`ParamTypeOf`, `Variant`).

## 1. Script delegates — FULL, both backends, certified
The seam that lets any native API take a **script function as a typed callback** (Traktor's
`IRuntimeDelegate`). Additive — it does NOT replace the physics contact bridge (that stays;
its deferred dispatch protects against scene mutation mid-physics-step).
- **Contract:** `class IScriptDelegate : public core::Object { virtual core::Result<Variant>
  Invoke(core::Span<Variant> args) = 0; }`. A native method receives one as
  `RefPtr<IScriptDelegate>`; holding it keeps the script function alive (GC-safe).
- **Marshalling:** when a reflected method parameter is typed `RefPtr<IScriptDelegate>` (or a
  `ScriptDelegate` value), a script **function/closure** argument marshals into a backend
  delegate. Extend `Core/Reflection` `ParamTypeOf`/`AcceptArg`/`ConvertArg` to recognize the
  delegate param type, and each backend's `MarshalIn` to wrap a callable slot:
  - Wren: a `Fn`/closure slot → `WrenScriptDelegate` holding a `WrenHandle*` to the fn + a
    `call(...)` call handle; `Invoke` marshals args and `wrenCall`s. Release the handle on
    destroy. (A `Fn` slot type isn't a reflected foreign type — grab it with
    `wrenGetSlotHandle` regardless of slot type, same trick the coroutine registration uses.)
  - AngelScript: a funcdef/function-handle arg → `AngelScriptDelegate` holding the
    `asIScriptFunction*` (AddRef; for a delegate, the bound `asIScriptObject*` owner too);
    `Invoke` prepares a context, sets args, executes.
- **Capability:** `ScriptCapabilities::Delegates`; both backends declare it.
- **Battery:** a section (runs when Delegates declared) that registers a native function
  taking a delegate, has a script pass a function, invokes it from C++ via the stored
  delegate, checks the returned Variant, and checks it still works after `CollectGarbage`
  (held delegate survives GC). Certify on Wren AND AngelScript.
- **Prove it with a use:** expose ONE real native API taking a delegate (smallest good
  example — e.g. a `Signal`/event facade a behavior subscribes to, or a timer callback), with
  a headless test. Rule: a seam lands with a user.

## 2. Backend API introspection — FULL, both backends
The **accurate** data source for the future ScriptClassesView/autocomplete: what's callable
in THIS language and how it's spelled (names/signatures differ per backend; a backend may not
bind everything). The reflection registry says what types *exist*; the backend says what's
*callable in script*.
- **Contract:** `IScriptManager::DescribeBoundApi(...)` returning a neutral structure — e.g.
  `Array<ScriptApiType{ String scriptName; bool isNamespace; Array<ScriptApiMember{ String
  name; String signature; bool isStatic; MemberKind (Method/Property/Constant) }> }>`. Each
  backend reports its ACTUAL bound surface with **script-visible names** (AngelScript
  `Float3::Dot`, Wren's spelling, value-vs-ref as the language presents it).
- **Conformance:** a battery check that every reflected engine type registered is present in
  the backend's reported surface — the reflection-vs-backend **diff catches a backend that
  silently failed to bind a type**. (This is the payoff the user specifically wanted.)
- No capability flag needed — every backend implements it (default returns empty for a
  backend that hasn't; the diff check then flags it). Keep it a standard method.

## 3–5. Debugger / Profiler / Cook-to-bytecode — INTERFACE + CAPABILITY + STUB ONLY
Lay the neutral seams down so the architecture is committed and future work slots in; each
backend declares the capability **absent** and the factory returns null. Do NOT fake them.
Model on Traktor (trimmed) — full impl + editor UI + remote transport is a later dedicated
track.
- **Debugger:** `class IScriptDebugger` — `SetBreakpoint(file,line)`/`RemoveBreakpoint`,
  `Break/Continue/StepInto/StepOver`, `CaptureStackFrame(depth)`, `CaptureLocals(depth)`,
  `CaptureObject(ref)` (lazy tree expand), a listener for async state change. Value snapshot
  types (`StackFrame{file,func,line}`, `Variable{name,type,value}`, `IValue`/`Value`/
  `ValueObject{ref,text}`) as small serializable structs — note these must serialize for a
  future remote transport, so keep them plain-data + wire-symmetric. `IScriptManager::
  CreateDebugger()` → null default. `ScriptCapabilities::Debugger` (absent for now).
- **Profiler:** `class IScriptProfiler` — listener with `CallMeasured(scriptId, function,
  callCount, inclusive, exclusive)`. `CreateProfiler()` → null. `ScriptCapabilities::Profiler`.
- **Bytecode blob:** `IScriptBlob` (opaque serializable compiled unit) + `IScriptManager::
  CompileToBlob(source)` / a context `LoadBlob`. Default: unsupported (returns error).
  `ScriptCapabilities::Bytecode`. (AngelScript can fill this later via `SaveByteCode`; Wren
  has no stable bytecode and stays source — the exact split the capability model exists for.)
- Conformance: a skipped-when-absent skeleton section for each, so turning a capability on
  later immediately has a certification target.

## Definition of done
- Both compilers clean (`cmake -S . -B build/clang -G Ninja -DCMAKE_BUILD_TYPE=Debug
  -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_C_COMPILER=/usr/bin/cc`; g++→build/gcc). Foreground,
  synchronous builds/tests.
- ALL ctest green both compilers (baseline will be whatever master is after the two prior
  merges). Delegate + introspection battery sections green on Wren AND AngelScript.
- Smokes: editor `--exit-after 8` → 0; player `--exit-after 5` → `game.wren: exit after`.
- Coherent `Script:` commits; leave the branch for review; do NOT merge.

## Repo rules
PascalCase, full names, UTF-8 char8_t, core containers, no std:: in public APIs. GCC module
hygiene: `wren.h`/`angelscript.h` + DRACONIC_REFLECT_* bodies in impl units only. Wire
read/write symmetry for any serializable struct (+ test) — the debugger snapshot types
especially, since they're built for a remote transport. Stage files explicitly; NO
Co-Authored-By/Claude-Session; never touch docs/, Bin/, ThirdParty/, scratch. `cd` to the
worktree root each shell call. Gotchas: `StringView::SubStr(offset,count)`; String
`Format()`; HashMap Find+InsertOrAssign; unused lambda captures are -Werror; Wren method
names can't start with `_`; a Python-edit assert before the write loses all edits.

## Report
The delegate contract + marshalling per backend + the one real use; the introspection
structure + the reflection-diff conformance check; the three stubbed seams (interfaces +
capability flags declared absent + skeleton conformance); battery results on both backends;
ctest totals both compilers + smokes; deviations with reasons.
