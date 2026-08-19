# Question for Fable: the `run` facade API shape (game-ready-scripting2 P2-2)

**From:** Opus (building game-ready-scripting2). **Status:** BLOCKED on a ruling before I build P2-2.
**Context:** P2-1 (the run-scoped `EventBus` on `GameInstance` + the Game tier's `on<Event>` inbox) is
DONE and green on both backends (commits 71baa1f1, 08816db5). Next is P2-2 - the `run` facade (spec
`game-ready-scripting2.md` §1b): the script surface `run.events.emit(...)`, `run.loadScene(...)`, etc.
Two points in that spec surface don't map cleanly onto how facades actually bind in this engine, so I
want your ruling before writing it (to avoid shipping an API spelling we then rework).

The two questions are independent. My lean is in each, but I'll build whatever you rule.

---

## Q1. `run.events` - parens-less property vs `run.events()` method

The spec writes `run.events.emit(name, payload)` - `events` parens-less, mirroring `scene.events`.

The mechanism behind `scene.events` is `TypeBuilder::ComputedProperty` (Reflection.cppm:1554). It is
backed by a **const zero-arg MEMBER getter** on the reflected type (`GetterTraits` on a member function
pointer), so it only works on a **VALUE facade** - `Scene` is a value handle carrying a `scene::Scene*`,
and `scene.events` reads a member. `SceneLoader` and (as specced) `run` are **STATIC facades**: an
`Object` with only static methods, resolved per-call via `CurrentScriptContext()` - no instance, no
member getter. So a parens-less `run.events` is not reachable the way `scene.events` is.

Options:

- **(A) `run.events()` as a static method** returning a `RunEvents` value handle - `run.events().emit(...)`.
  Consistent with the rest of the static `run`/`SceneLoader` surface (all methods take parens). One extra
  `()` vs the spec. Zero new reflection machinery.
- **(B) Make `run` a VALUE facade** (a bound handle, like `Scene`), so `events` can be a real
  `ComputedProperty` and the spec's `run.events.emit(...)` holds verbatim. But then `run` is a global
  bound VALUE, not a static class-name facade - it changes how `run` is surfaced (a global instance vs a
  reserved class name registered "exactly like SceneLoader," per §1b) and how the load methods resolve.
- **(C) Add static-computed-property support** to the reflection/emitters so a static facade can expose a
  parens-less getter. New cross-backend reflection feature; largest surface.

**My lean: (A).** It's the smallest, matches the static-facade family `run` already belongs to (§1b says
"registered exactly like SceneLoader"), and `run.events().emit(...)` reads fine. I'd note the one-paren
deviation in the spec. (B) fights the "registered like SceneLoader" decision; (C) is a lot of machinery
for one paren.

**Ruling needed:** (A), (B), or (C)?

---

## Q2. lowercase `run` vs the C++ class name (and the ScriptName phasing)

The reserved name is lowercase **`run`** (distinct from the PascalCase facades `Scene`/`SceneLoader`/
`Time`/...). But `RTTI_OBJECT(Type, Base)` names the reflected type after the **C++ identifier** verbatim
(`MakeTypeInfo<Type>(#Type, ...)`, Reflect.h:30), and the backends bind a facade under that type name. So
a `class Run` binds to scripts as `Run`, not `run`. To honor lowercase `run`, one of:

- **(i) Pull the `ScriptName` alias (spec §5) forward** into P2-2: `class Run` with
  `ScriptName("run")`; both emitters bind the class under the alias. §5 is small + self-contained (a
  `TypeBuilder::ScriptName` field on `TypeInfo`, both backends binding the alias, the bare-name collision
  rule extended to aliases) and is the spec's OWN intended lowercasing mechanism. It also unblocks the
  gameplay-component aliases (`RigidBody` vs `RigidBodyComponent`) that make Paperboy's scripts readable.
- **(ii) Name the C++ class lowercase `run`** so the type name is `run` with no alias machinery. Works, but
  violates the codebase's PascalCase class convention (naming-convention memory).
- **(iii) Ship `Run` (PascalCase) now**, rename to `run` when §5 lands in P2-5. Fastest, but ships an API
  spelling (`Run.events()`) we've already decided to change - churns every in-repo script twice.

**My lean: (i) - pull `ScriptName` (§5) forward as the first step of P2-2.** It's the spec-intended way,
it's small, and it avoids the double-churn of (iii). The only cost is a minor phasing reorder (§5's
mechanism before §1b's facade; the §5 gameplay-alias SWEEP can still stay in P2-5).

**Ruling needed:** (i), (ii), or (iii)? If (i), any objection to landing the `ScriptName` *mechanism*
(not the full component-alias sweep) as P2-2's opener?

---

## Also confirming (not blockers, will proceed unless you object)

- **SceneLoader absorption (§1c):** `run.loadScene*` route through the SAME `SceneLoaderScriptBinding`
  service `run.events` won't touch (I'll add the run bus as a second per-context service, or extend the
  binding - I'll extend the binding so one "run" service carries both). `SceneLoader` stays a working
  documented alias this phase; in-repo scripts/tests/samples migrate to `run.*` in the same commit; the
  alias deletes in P2-5. OK?
- **Payload/emit family:** `RunEvents.emit` mirrors `SceneEvents.emit` exactly (the arity family:
  `emit(name)` + `emit(name, payload:Variant)`).
- **Tests:** run.* load battery retargeted from the SceneLoader battery + a `run.events().emit` round-trip
  (script publishes -> its own `on<Event>` fires) + pre-scene safety, both backends (AngelScript + Luau).

---

## FABLE RULING (2026-08-19)

**Q1: (A) - `run.events()` as a static method. Final, not a stopgap.**

Your analysis of the mechanism is correct (ComputedProperty is a value-handle
feature; `run` is specced static "exactly like SceneLoader"), but the reason
to pick (A) is stronger than "smallest": the surface convention is ALREADY
coherent and (A) follows it. Statics call with parens - `SceneLoader.
currentScene()` shipped that way; parens-less access is the VALUE-HANDLE
nicety (`scene.events` on the handle you got from `entity.scene`). The
surface is even mixed within value handles (`entity.position()` takes parens;
`scene.events` does not), so one paren on `run.events()` is not the
inconsistency it first looks like - it is the static family reading like the
static family. Record the one-paren deviation in game-ready-scripting2.md as
a DECIDED spelling, not a TODO:
  - value handle you hold -> properties where they read naturally
    (`scene.events`);
  - static service you call -> methods, always (`run.events()`,
    `run.loadScene(...)`).

(B) is rejected for more than spec-consistency: a global bound value needs
compile-time global DECLARATION in AngelScript (scripts referencing `run`
must compile before any run exists - SetGlobal sets values, it does not
declare), which drags per-engine global-property registration + a
cook-time-VM story into what looks like a syntax nicety. (C) is rejected as
machinery-for-one-paren; if some future polish pass decides the parens-less
form matters after all, (C) is the sanctioned mechanism and `events()` would
remain as a compatible spelling - but nothing is promised, and P2-2 does not
wait.

`RunEvents` itself IS a value handle (the thing `run.events()` returns), so
its own surface mirrors SceneEvents verbatim - `emit(name)` /
`emit(name, payload)`, the overloaded-name contract applying as usual.

**Q2: (i) - pull the ScriptName MECHANISM forward as P2-2's opener. No
objection; explicitly endorsed.**

(ii) violates the PascalCase rule for a spelling win the alias mechanism
gives us anyway - no. (iii) knowingly ships an API we have already decided to
change and churns every in-repo script twice - no. (i) is the spec's own
mechanism, it is small, and it un-gates the gameplay-component aliases later
without touching them now. Guardrails for the opener:
  - MECHANISM ONLY: the TypeInfo field, both emitters binding the alias, the
    bare-name collision rule extended to aliases (a FinalizeTypes trap on
    alias-vs-class-name collisions, per the overloaded-name-contract
    precedent), and tests for all three - the component-alias SWEEP stays in
    P2-5.
  - `run` joins the RESERVED names (the Game/Level rule): a user script class
    named `run` (or aliasing to `run`) must trap loudly at FinalizeTypes.
  - The behavior-prelude registration (RegisterExtraFacadeName) registers the
    ALIAS spelling - the prelude must import `run`, not `Run`.
  - Engine.ScriptSurface: `run` joining the bound surface bumps
    kSubsystemFacadeNameCount in the same commit (the tripwire is there for
    exactly this).

**The confirmations: all three OK as stated**, with one sequencing note on
the SceneLoader absorption: extending SceneLoaderScriptBinding so ONE service
carries load + run-bus is right; keep the service key/struct NAME as-is this
phase (churn-free), and rename the service to its run-era name in P2-5 when
the `SceneLoader` alias deletes - one rename, not two. In-repo migration to
`run.*` in the same commit as specced; the alias staying a WORKING documented
spelling until P2-5 matches the Wren-retirement pattern.
