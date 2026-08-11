# Handoff: complete the Draconic scripting track (P2 + P3)

> **PROGRESS (updated 2026-07-19, branch `scripting-p2`, base master @ `6fb4cfa`):**
> Two slices are DONE, committed, and verified (ctest 85/85 clang+gcc, editor+player
> smokes green):
> - **P2.2 `entity.send` — DONE** (`9523d5f`). `entity.send(name[, arg])` invokes
>   `on<Name>(arg)` on every behavior of the target that declares it. Typed overloads
>   (number/string/entity), resolved by arg type. **KEY FINDING baked into the design:**
>   delivery MUST be DEFERRED — a send fires inside a running script call and **Wren
>   forbids re-entrant `wrenCall`** (a synchronous dispatch segfaults). Messages queue and
>   drain at the tick's top level (`ScriptSceneSystem::EnqueueMessage`/`DrainMessages`,
>   drained in `OnUpdate` after `TickBehaviors`), same frame, never nested, capped at 4096
>   to break send loops. The same queue is the right vehicle for physics events (P2.1).
>   The cook handler scan is now generalized from a fixed list to the whole
>   `on<Upper>(...)` convention, so custom message handlers AND the reserved event
>   handlers are harvested + `HasHandler`-gated for free.
> - **P3.1 `updateInterval` — DONE** (`2a4c6b7`). Per-behavior onUpdate throttling;
>   delivers the accumulated dt; serialized (wire-symmetric) + inspector row.
> - **P2.4 Scene.spawn — DONE** (`bb65c8c`). `Scene.spawn(prefab, x, y, z)` spawns a
>   prefab into the current scene and returns the root as an `Entity`. The binding gained
>   `currentScene` (subsystem sets it each OnUpdate) + a `spawnPrefab` callback the host
>   installs; `DefaultApplication` wires the real content-DB-backed spawner. Headless test
>   via a fake spawner + an `asset:Prefab` property.
> - **P2.4 Input/Audio/Physics facades on behaviors — ALREADY WIRED** (verified, no
>   change needed): `DefaultApplication::Configure` sets `SetContextConfigurator` on the
>   ScriptSubsystem, so every run context (incl. the behavior context) gets
>   `input/physics/audio->ExposeToScript`. Behaviors reach them via `import "main" for
>   Input` (they're reflected into the context's "main" module, not the auto-prelude).
>
> **REMAINING: P2.1 physics events (needs the decision below), P2.3 fibers, P2.5
> ScriptPage, P3.2 live-state reload, P3.3 tooling hooks. NOTE: honor the "facade lands
> with a sample" rule — the new send/spawn facades have headless tests but the windowed
> ScriptPlayground sample should also demonstrate them (spawn needs a cooked prefab in the
> sample project).**
>
> **DECISION NEEDED before P2.1 (physics events):** the body→entity reverse-map. A physics
> body stores only `scene.GetEntityId(e).low` (the entity GUID's low 64 bits) in its user
> word; `ContactEvent` carries `userA`/`userB` as those low words. But `Scene::FindEntity`
> needs a FULL 128-bit `Guid`, and nothing currently bridges contacts to entities. So P2.1
> needs a dependency-direction call: either (a) the physics subsystem exposes a
> `userWord → EntityHandle` map (or pushes contacts into a script seam), or (b) the script
> subsystem builds its own `low → EntityHandle` map each frame by walking
> `RigidBodyComponent` owners (adds a physics dependency to the script subsystem). Pick
> deliberately — don't rush cross-subsystem coupling. Then reuse the deferred `EnqueueMessage`
> queue to deliver `onContactBegin/End(other, ...)`.


You are an Opus agent completing the scripting feature in the **Draconic** engine
(C++23 modules, CMake + Ninja, dual clang+gcc Debug builds, Linux). The foundation is
**done and merged**: a backend-neutral scripting layer (Wren + AngelScript, both
certified against a shared conformance battery) and **behaviors P1** (entity behavior
scripts with a cooked asset pipeline, inspector, hot reload, and `entity`/`Log`/`Time`/
`Random` facades). Your job is **P2** (events + fibers + the full facade surface +
ScriptPage editor) and then **P3** (polish). This document is your spec.

The design doc is authoritative and user-approved: **`docs/design/scripting.md`** — read
it in full, especially §3 (behaviors design, incl. §3.4 events and §3.5 API surface),
§4 (runtime resources), §6 (errors), §7 (phasing: you are doing P2 then P3), §7.5
(backend neutrality — already shipped, but it constrains HOW you add things), §8
(answered questions), and the 2026-07-18 addendum (host-object injection / the SetGlobal
stub — this shapes every facade). **`docs/` is untracked and read-only reference — never
edit or commit anything under `docs/`.** If a design decision in this handoff seems to
conflict with the doc, the doc wins; surface the conflict in your report rather than
guessing.

---

## Ground truth: what P1 already gives you (read this code before writing)

- **`Code/Foundation/Script/`** contract: `IScriptManager` (+ `FinalizeTypes()`,
  `Capabilities()`), `IScriptContext` (Load/Call/Invoke/CreateInstance/GetGlobal/
  SetGlobal/HasFunction/SetService/GetService/SetErrorHandler), `Variant`,
  `BackendRegistry.cppm` (`CreateScriptManagerForLanguage`/`ForFile` — the ONLY way to
  make a manager; never name a backend type), `ScriptCapabilities` flags, and the
  conformance battery `Tests/BackendConformance.h` (**do not weaken its assertions**).
- **`Code/Foundation/Script/Wren/`** and **`.../AngelScript/`**: the two backends. Wren
  declares `ScriptCapabilities::Fibers`; AngelScript declares `None`.
- **`Code/Foundation/Script/Resource/ScriptResource.cppm`**: cooked `ScriptClassSource` →
  runtime `ScriptClass` = { source text, `className`, `properties[]` (name, hash, type,
  default Variant, description), **`handlers[]`** (declared method names — the dispatch
  gate), version }. `ScriptClass::HasHandler(StringView)` is how dispatch avoids
  per-frame method-missing probing.
- **`Code/Foundation/Script/Editor/ScriptAsset.cppm`**: the cook builder. It compiles the
  class in a **cooker-owned VM resolved via the registry** and appends a Wren **probe**
  that serializes the `static properties` map into a `\x1F`/`\x1E`-delimited string,
  parsed back to typed metadata. **The handler set is harvested here too** — when you add
  new handler names (P2 event methods), the harvest probe is what must learn to record
  them.
- **`Code/Foundation/Script/Subsystem/`**:
  - `Components.cppm`: `ScriptComponent { Array<ScriptBehavior> behaviors }`;
    `ScriptBehavior { Ref<ScriptClass> script, bool enabled, Array<ScriptPropertyOverride
    {u64 nameHash, ScriptPropertyValue value}> overrides, [runtime] instance, boundClass,
    started }`. **Wire rule**: `ScriptPropertyValue` writes its `kind` tag FIRST so
    writer/reader branch identically; arrays are count-prefixed. Any new serialized field
    must keep write/read symmetric — a past bug (unconditional write + gated read)
    corrupted the stream and caused an OOM loop. Add a symmetry test for anything new.
  - `Subsystem.cppm`: `ScriptRunHost` (plain class, no device deps — owns the run's ONE
    gameplay `IScriptContext`, created at run start, destroyed at Stop — the locked PIE
    rule; `SetService(kScriptRuntimeService, &m_binding)`); `ScriptSceneSystem`
    (per-scene `SceneSystem`: deferred-start queue, `OnUpdate` dispatch, enable/disable
    edges, `OnSceneStopped` teardown); `ScriptSubsystem` (the `runtime::Subsystem`
    wrapper). Dispatch goes through **`InvokeHandler(behavior, scriptClass, entity,
    method, args)`**, gated by `scriptClass.HasHandler(method)`. Handler name constants
    live here: `kOnStart`/`kOnUpdate`/`kOnEnable`/`kOnDisable`/`kOnDestroy`. **This is the
    single choke point you extend for every new event.**
- **`Code/Foundation/Script/Facades/ScriptFacades.cppm`**: the behavior-visible API.
  `kScriptRuntimeService = u8"script.runtime"`; `ScriptRuntimeBinding { f64 timeSeconds,
  f32 deltaSeconds, core::Random random }`. Facades are **reflected foreign classes with
  static methods** that resolve their per-context binding via
  `context->GetService(kScriptRuntimeService)` — because **`WrenScriptContext::SetGlobal`
  is a stub** (Wren's C API can't inject host globals). `Entity` (value handle: isValid/
  name/setName/position/setPosition/worldPosition/setRotationEuler/setScale/destroy),
  `Log`, `Time`, `Random`. `RegisterScriptFacadeReflection()` registers them.
- **`Code/Foundation/Runtime/DefaultApp/DefaultApplication.cppm`**: wires it together —
  `RegisterReflectedTypes`, `RegisterScriptFacadeReflection`, and note it already calls
  **`m_input->ExposeToScript(context)`, `m_physics->ExposeToScript(context)`,
  `m_audio->ExposeToScript(context, Resources())`, `audio::RegisterAudioScriptApi()`** for
  the **game-script** context. **Key P2 integration question to resolve first (see P2.4):
  does the `ScriptRunHost`'s behavior context receive those same `ExposeToScript` calls?**
  If not, behaviors can't see Input/Physics/Audio yet — that wiring is part of P2.
- **`Code/Samples/ScriptPlayground/main.cpp`**: the P1 sample (Mover/Spinner). Extend it
  (or add tabs/scenes) to demo each P2 facility with a real behavior — the standing rule:
  **a facade lands only WITH a sample that exercises it.**

**Physics event seam you will consume (P2.1):** `Code/Foundation/Physics/World.cppm` —
`struct ContactEvent { ContactKind kind; BodyId bodyA, bodyB; u64 userA, userB; }` and
`DrainContacts(Array<ContactEvent>&)`; `Code/Foundation/Physics/Subsystem/Subsystem.cppm`
drains per fixed step and exposes `Events()` (a `Span<const ContactEvent>`). The `userA/
userB` are the per-body user words — determine what they encode (entity handle vs body
id) and map back to entities. This is the buffered, main-thread contact stream §3.4
reserves for `onContact*`.

---

## P2 — events + fibers + facades + ScriptPage

Deliver these. Each is independently testable; commit them as coherent units. Every new
handler name must be (a) harvested by the cook probe, (b) gated by `HasHandler`, (c)
dispatched through `InvokeHandler`, (d) covered by a headless test, (e) shown in a sample.

### P2.1 — Physics events into behaviors
Handler methods (design §3.4): `onContactBegin(other, point, normal, impulse)` and
`onContactEnd(other)`; `onTriggerEnter(other)` / `onTriggerExit(other)` if the physics
layer distinguishes triggers (check `ContactKind`; if triggers aren't separated yet,
implement the contact pair and note triggers as deferred with the reason).
- Consume the physics subsystem's per-step `ContactEvent` buffer on the main thread, map
  each body to its entity + `ScriptComponent`, and invoke the handler on every behavior
  of both entities that declares it, passing the other entity as an `Entity` handle plus
  the contact data marshalled as Variants. Respect the enabled/simulation gates.
- Order/ownership: dispatch during the script update phase after physics has stepped and
  drained; do not reach into Jolt directly — go through the physics subsystem's public
  event surface. Don't double-deliver (a Begin/End pair per contact, per entity).
- Test (headless): a scripted scene with two dynamic bodies; assert `onContactBegin`
  fires with the correct `other` entity when they collide. If a headless physics step is
  awkward, drive the subsystem's event buffer directly in the test and assert dispatch.

### P2.2 — `entity.send` (behavior-to-behavior messaging)
`entity.send("heal", [amount])` invokes `onHeal(amount)` on **every** behavior of the
target entity that declares it (Lumix's generic call-in — no typed message registry).
- Add `send(name, args)` to the `Entity` facade. It needs a route from a facade static
  back into the subsystem's dispatch — extend `ScriptRuntimeBinding` (or add a dedicated
  service) with a pointer/callback the facade calls; the subsystem resolves the target
  entity's component and invokes `on<Name>` via `InvokeHandler`, respecting `HasHandler`.
  Method name = `"on" + Capitalize(name)`; document the convention in code and the sample.
- Beware reentrancy: a `send` during dispatch may add/destroy behaviors. Mirror the
  existing dispatch's safety (deferred structural changes / snapshot iteration) — study
  how `ScriptSceneSystem` already guards its update loop, and if it doesn't, add the
  guard and a test that sends within a handler.
- Test: entity with two behaviors; one sends `"ping"`, assert the other's `onPing` ran.

### P2.3 — Fiber scheduler (coroutines) + Wait/Tween stdlib
Wren has first-class fibers and declares `ScriptCapabilities::Fibers` — the scheduler is
thin. Consume the capability flag: **if `manager->Capabilities()` lacks `Fibers`, the
scheduler degrades to a no-op and `wait` throws a clear scripting error** (AngelScript
path) rather than breaking the model. Do not hard-depend on Wren.
- Script-side stdlib (a cooked utility script module, or a reflected helper — match how
  the facade prelude is injected in `ScriptAsset`/`Facades`): `this.wait(seconds)`,
  `this.waitUntil(fn)` yield the behavior's fiber; ship `Wait`/`MoveTo`/`Tween` helpers
  (§3.3). The subsystem resumes due fibers each tick and **stops all fibers of an instance
  on destroy/disable** (the ez teardown rule).
- Implementation: the subsystem tracks per-instance suspended fibers with their resume
  condition (elapsed time or predicate), resumes them in the update phase, and drops them
  on teardown. Keep the C++ side backend-neutral (drive it through `ScriptObject::Invoke`
  / the context) — the Wren-specific fiber primitive lives behind the script stdlib and
  the capability check, not in the subsystem's core loop.
- Tests (headless): `wait(dt)` resumes after the right accumulated time; `waitUntil`
  resumes when its predicate flips; a destroyed instance's pending fiber does not resume.

### P2.4 — Full facade surface for behaviors (Input / Audio / Physics / Scene.spawn)
The behavior context is the run context, so behaviors should reach the same curated
gameplay API the game script has. First resolve the integration question above: ensure
the `ScriptRunHost` context gets `Input`/`Physics`/`Audio` exposed (call the existing
`ExposeToScript`/`RegisterAudioScriptApi` on it, or refactor so both the game-script
context and the behavior context share one exposure path — prefer one path).
- `Scene.spawn(prefabRef, transform)` — wire to the existing prefab runtime spawner
  (it exists; find it in the scene/prefab code). Returns an `Entity` handle.
- `Input.action(...)` (poll — `input.md §6`), `Audio.playOneShot(...)` (`audio.md §6`),
  `Physics.rayCast(...)` (`physics.md §6`): these facades may already exist for the game
  tier — reuse them, don't re-author. If a facade is missing for a subsystem, add it
  following the static-method-over-service pattern and that subsystem's doc §6.
- **Rule**: each facade you surface lands with a sample behavior that uses it. No
  API-first surface. If a subsystem's P1 isn't landed enough to back a facade, defer that
  facade explicitly with the reason in your report.
- Tests: `Scene.spawn` from a behavior creates the entity (headless); a raycast/action
  poll returns a sane value against a fixture. Keep device-free where possible.

### P2.5 — ScriptPage (editor)
A text page for editing behavior scripts, modeled on the existing `UIDocumentPage` (find
it under `Code/Draconic/Editor/`). Inline compile-error surfacing (reuse the cook
`ScriptError` file:line already produced by the builder), save → recook → hot reload
(the P1 reload path already works — hook the page's save into it). Undo/redo via the
page's text buffer. Follow the editor's page registration idiom. UI actions that destroy
views/controls must defer via `UIContext MutationQueueRef().QueueAction(...)` — never
mid-event-dispatch. Editor font renders only codepoints ≤255 — ASCII markers in chrome.
- Verify via the editor smoke (it must still exit cleanly) plus, if feasible, a headless
  test of the save→recook→reload path (that path is already covered by P1's reload test;
  extend it if the page adds logic).

---

## P3 — polish (do after P2 is green; smaller, still tested)

- **Per-behavior `updateInterval`** (ez throttling, §3.3): optional per-behavior tick
  interval; the subsystem accumulates and calls `onUpdate` at the requested cadence
  (passing the accumulated dt). Serialized on the behavior (wire symmetry!), editable in
  the inspector. Test: a behavior with interval N gets ~1 update per N seconds of
  accumulated time.
- **Live-state-preserving reload** (Godot placeholder model, §5): today reload
  re-instantiates and re-applies editor overrides but resets transient script state. P3
  refinement: preserve declared-property live values across reload where the property
  still exists (migrate by name/hash), resetting only removed/retyped ones. Document
  exactly what survives. Test: mutate a property at runtime, reload, assert it survived
  while a transient field reset.
- **Script-defined editor tooling hooks** (deferred item, §7 P3): keep minimal and tie to
  the edit-time-Configure questions from the roadmap — if it's ambiguous, spec it in the
  doc-style comment and defer implementation with a note rather than over-building.

Optional (only if time allows, tied to B4/Traktor seams in §7.5 — otherwise leave for a
future phase and say so): a `ScriptCapabilities::Profiler` consumer (per-behavior cost is
already under `PROFILE_SCOPE`), or the `IScriptCompiler → IScriptBlob` cook-time
compile seam. Do NOT implement a debugger.

---

## Definition of done (all of it, both compilers)
1. Every P2 item + every P3 item above implemented, or explicitly deferred **in your
   report with a concrete reason** (a missing subsystem dependency is a valid reason; "ran
   low on time" is not — finish or hand back cleanly).
2. **Tests for everything** (repo rule — nothing is "done" without tests). Prefer
   **headless** doctest suites in the module `Tests/` dirs (no window/GPU). New handler →
   harvest + dispatch test; new facade → behavioral test; new serialized field → wire
   symmetry test. Extend `Code/Foundation/Script/Subsystem/Tests/ScriptSceneTests.cpp` and
   friends.
3. **`ctest` 100% on BOTH clang and gcc.** Current baseline is **85 suites** — you add
   more. Configure: `cmake -S . -B build/clang -G Ninja -DCMAKE_BUILD_TYPE=Debug
   -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_C_COMPILER=/usr/bin/cc` (and the same with `g++`
   → `build/gcc`). Build+test **synchronously in the foreground** with generous timeouts;
   never background-and-wait for a notification.
4. **Smokes** at final HEAD:
   - `timeout 120 Bin/Debug/Linux64-Clang/DraconicEditor build/clang/Code/Tools/Editor/EditorProject --exit-after 8` → exit 0.
   - `timeout 60 Bin/Debug/Linux64-Clang/DraconicPlayer build/clang/Code/Tools/Editor/EditorProject --exit-after 5` → prints `game.wren: exit after`.
   - The `ScriptPlayground` sample builds and runs (it's windowed and does NOT honor
     `--exit-after` — running ~10s without crashing is the bar; confirm your new behaviors'
     log lines appear).
5. Coherent commits on your worktree branch; leave the branch for review (do **not** merge
   to master yourself). End with the report below.

## Repo rules (violations get the work rejected)
- **PascalCase** methods/functions; **full descriptive names** (no abbreviations); the
  script-visible facade method names are camelCase by the Audio/Input precedent (match
  existing facades). **UTF-8 `char8_t`** strings (`u8"..."`, `String`/`StringView`); core
  containers (`Array`/`HashMap`/`RefPtr`/`UniquePtr`/`Function`); **no `std::` in public
  APIs**.
- **GCC module hygiene**: heavy third-party headers (`angelscript.h`, `wren.h`, Jolt) and
  `DRACONIC_REFLECT_*` macro BODIES go in module **implementation** units (`*Impl.cpp`),
  never interface units — GCC writes unreadable gcm clusters otherwise. The P1 facade
  split (`ScriptFacades.cppm` + `ScriptFacadesImpl.cpp`) is your template.
- **Wire read/write symmetry**, count-prefixed arrays, tag-first tagged unions — as P1
  does. Test any new serialized state.
- Value-pool scene components follow the manager idiom in `Code/Draconic/Scene/` and the
  Audio/Physics subsystem precedent (ISceneAware, snapshot walks).
- **Backend neutrality is load-bearing**: resolve managers via the registry; drive scripts
  through `IScriptContext`/`ScriptObject`; gate optional features on
  `manager->Capabilities()`. The only place Wren-specific code is allowed is behind a
  capability check or inside the Wren backend/stdlib — never in the subsystem core.
- **Never** touch `docs/`, `Bin/` (the user's EditorProject lives under
  `Bin/Debug/Linux64-Clang/EditorProject` — deleting it once cost real work), `ThirdParty/`,
  the conformance battery's assertions, or the Wren/AngelScript backend behavior (extend,
  don't rewrite). **Stage files explicitly** (`git add <paths>`) — never `git add -A`.
  **No `Co-Authored-By` / `Claude-Session` trailers.** Delete scratch/`*.log` files before
  committing.
- `cd` to the worktree root at the start of **every** shell command — cwd drifts.
- Wren frictions already found (don't rediscover): single-line `{ ... }` blocks parse as
  **expression** bodies — statement blocks must be multiline; lowercase module-level `var`
  names aren't visible inside class methods — module globals scripts reference must be
  **Capitalized**; `SetGlobal` is a stub → facades use static-method-over-service (the
  addendum). AngelScript: generic-call mode makes the callee own `Type@` handle args
  (release after marshalling); `CreateInstance` must skip the implicit copy factory.
- API gotchas: `StringView::SubStr(offset, count)`; `String` has `Format()`/`FormatFixed()`
  (no AppendFormat); `HashMap` has Find + InsertOrAssign (no GetOrInsert);
  `Settings::Section<T>()` returns a reference; unused lambda captures are `-Werror`.

## Final report (include all of it)
Per P2 item and P3 item: what shipped, the exact script-visible API added (with the sample
behavior source that uses it), and where it hooks into the C++ (handler name, dispatch
site, facade→subsystem route). For events: how you mapped physics bodies to entities and
how you avoided double-delivery. For fibers: how the scheduler stays backend-neutral and
what happens under a no-Fibers backend. Any new serialized fields + their symmetry tests.
Test counts and `ctest` totals on both compilers; smoke results. Anything deferred, with
the concrete dependency or reason. Contract frictions or design-doc conflicts the
maintainer should know about.
