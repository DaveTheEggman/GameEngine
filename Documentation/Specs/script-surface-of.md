# Script surface: retire the facade layer via `.of` on reflected real types

Status: CLOSED (user accepted the audit verdict, 2026-08-22). Deliverable = P0 + the
physics explicit-hit fix. The .of(context) conversion is NOT built - the five static
facades are the sanctioned curated script views; the shipped .of(entity) and
.of(scene) axes are the idiom. Reopening subsystem reflection = its own architecture
evaluation (an Ideas doc: "Subsystem joins the Object/RTTI hierarchy"), never a
facade-cleanup side effect.
Spec 3 of the three-spec cut. UI IS EXPLICITLY OUT OF SCOPE - the user has flagged the
ui surface for its own separate examination; the `ui` facade and gamekit script
surface are untouched by this spec.

## The decisions (locked in the ideas-doc exchange)

- **`.of` is the one idiom, two axes**: `X.of(context)` for subsystems (context-scoped
  by definition), `Y.of(scene)` for per-scene systems. The return type carries the
  scope. Components already do this (`RigidBody.of(entity)`) - this spec widens the
  same mechanism, it does not invent one.
- **Curation moves onto the real type**: a subsystem exposes a deliberate subset via
  `REFLECT_MEMBERS` on ITSELF (methods point at the real impl - the Traktor
  `addMethod(&Real::method)` model). The resolve-service-then-forward wrapper classes
  are the thing being deleted.
- **NO backward-compat constraint** (user ruling): clean breaks; internal usage +
  tests update in the same commit; no alias shims, no both-patterns transition.
- **The semantic rule** (the survivor of the review): a reflected script view exists
  ONLY where the raw method cannot carry required semantics - deferral, marshalling,
  safety. Never for stability. Views are the exception, not the norm, under no-compat.
- **Tiers get handles, not the god object**: game = context + injected coordinator
  handle; scene (Level) = context + Scene handle; behavior = context + Scene + owner.
  All three reach the engine through the identical `.of` idiom. Scene loading /
  requestExit live on the coordinator (GameInstance-backed), NEVER on the
  scene-agnostic context.
- Inheritance sugar per backend is allowed on top but never load-bearing (Luau
  metatables vs AngelScript single-inheritance diverge); the mechanism is lifecycle
  names + injection + `.of`.

## The audit table (P0 finalizes; first-pass classification confirmed by the user)

| Surface | Classification | Notes |
|---|---|---|
| Audio (bus/music/one-shot) | direct-reflect | THE PILOT: small, no dispatch hazards |
| Input (map/rebind queries) | direct-reflect | |
| Navigation (agent ops) | direct-reflect | NavAgent.of(entity) already exists |
| Render (debug toggles etc.) | direct-reflect | curate hard - most of RenderSubsystem is not script business |
| Physics queries | direct-reflect + API fix | the facade's per-scene `lastHit` statefulness is a facade ARTIFACT: under no-compat, raycasts return an explicit hit-result value and the stored-state surface deletes |
| Net | `NetworkController.of(context)` | AFTER networking-extraction lands; the controller may move engine.gameinstance -> engine.net at this point (the one-move budget reserved in that spec) |
| run (loadScene/requestExit/...) | injected coordinator handle | migrates LAST; `run.events()` resolves to the messaging-spec bus |
| Scene / Entity / owner handles | keep as-is | already the right shape |
| ui / gamekit | OUT OF SCOPE | separate examination (user); the mutation-queue deferral semantics make it the hard case |

## Phasing

- **P0 - the audit.** Walk all ~15 registrars; finalize the table above per facade
  (each row names: direct-reflect / view / handle / delete, and for views WHICH
  semantic forces it). Output = the table committed into this spec. Anything the audit
  finds script-visible but unused gets DELETED, not migrated.
- **P1 - the `.of(context)` infrastructure.** The context handle as a bound value type
  (per-tier injection, same machinery as the Scene handle); a reflected static
  factory per participating subsystem type; cook-VM registration verified (two-phase
  declare/bind covers it - a cook-VM compile test per participating type is part of
  this phase). The `.of(scene)` axis already exists via the component/scene-system
  pattern.
- **P2 - the pilot: audio.** AudioSubsystem reflects its curated subset; scripts move
  to `AudioSubsystem.of(context)`; the Audio facade DELETES in the same commit with
  parity tests (every old op exercised through the new spelling - the Ui-facade
  hard-remove precedent); kSubsystemFacadeNameCount adjusts. The pilot also settles
  the mechanical questions (overload flattening on real methods per the
  overloaded-name contract; natural-types marshalling on real signatures) before the
  sweep.
- **P3 - the sweep.** Input, navigation, render, physics (with the explicit
  hit-result API fix), each surface cut over WHOLE, one commit per surface, parity
  tests each, facade deleted each.
- **P4 - the tiers.** The injected coordinator handle for the Game tier (the `run`
  facade's load/exit surface migrates onto it; PaperKid updates in the same commit);
  Net facade -> `NetworkController.of(context)`. LAST because run is
  PaperKid-load-bearing and Net waits on spec 1.

## Rules carried from the standing contracts

- Overloaded-name contract applies to reflected real methods (same-arity needs
  OverloadedName; send/emit stay Variant).
- Natural C++ types (i32/i64, never f64-flattening) on everything script-visible.
- kSubsystemFacadeNameCount tracks the bound surface through every phase - the count
  moves deliberately per commit, never silently.
- script_api output + Shipping/Scripting.md + Systems/scripting.md update in the same
  commit as each surface cutover (product surface; the doc-sweep lesson).
- Full two-compiler battery per landing; ASAN when the marshalling/executor layer is
  touched (P1 and the pilot).

## What this deliberately is NOT

Not a ui migration (separate examination). Not a change to components, Scene/Entity
handles, entity.send, or the behavior lifecycle. Not Traktor-style native script
inheritance or ref-typed components (rejected in the ideas doc). Not a both-patterns
transition - each surface is either old or new, never both.

## P0 AUDIT (Fable, 2026-08-22) - the verdict changes the spec

### What the audit found (all verified in code)

1. **The `.of(scene)` axis already dominates.** ScenePhysics/SceneRender/SceneParticles/
   SceneAudio/SceneAnimation are ALREADY `.of(scene)` bound value handles over real
   per-entity engine ops (resource swaps, play/stop, impulses) that component
   `.of(entity)` cannot express (they need the world/manager, not component data). They
   are NOT forward-stubs; they are the pattern, shipped. NO WORK.
2. **The true "resolve-service-then-forward" statics are five**: Audio, Input, Net, run,
   ui (out of scope). Post networking-P4, Net is already a thin view over
   INetworkController - the extraction the ideas doc wanted has happened.
3. **THE BLOCKING FACT: `Subsystem` is entirely OUTSIDE the reflection system.** It is
   not Object-derived and has no RTTI identity; the reflection layer knows value types
   and Object types only. `AudioSubsystem.of(context)` therefore requires: a new
   reflected type category (or rebasing Subsystem onto Object), a resolving-handle
   mechanism for non-owned subsystem pointers (Variant's object mode OWNS RefPtr - a
   subsystem is UniquePtr-owned by the Context, so object-mode marshalling is a
   double-delete; the component RESOLVE-variant machinery would need a subsystem
   flavor), plus a Context handle type placed below every subsystem lib.
4. **The stubs would RELOCATE, not die.** The facade methods are not pure forwards -
   they MARSHAL (resolve the per-context service carrying subsystem + resource manager,
   adapt signatures like PlayOneShotByPath(ResourceManager&, path) -> playOneShot(path)).
   Reflecting "the real type" means the real type grows exactly these script-shaped
   wrappers. Net method-count delta: ~zero. Deleted per domain: one class + one service
   key + one registrar line. Added: runtime-layer RTTI surgery + a longer call spelling
   (`Audio.playOneShot(p)` -> `AudioSubsystem.of(ctx).playOneShot(p)`).
5. **No dead surface found.** Every facade op has live consumers (tests, PaperKid,
   samples). Nothing qualified for the delete rule.

### The verdict

The spec's core conversion premise - "curation moves onto the real type, the wrapper
layer dies" - came from Traktor, where EVERYTHING lives in one RTTI world. Here the
static facades have already converged into exactly what the semantic rule sanctions:
thin, curated SCRIPT VIEWS doing real marshalling over per-context services. Converting
them to `.of(context)` costs foundational runtime surgery and yields relocated stubs
with worse spellings.

RECOMMENDATION: do NOT build the `.of(context)` conversion. Keep the five statics as
the sanctioned script views (they are small, tested, and product-proven by PaperKid);
keep the two axes that already ship (`X.of(entity)` components, `SceneX.of(scene)`
world ops). If the user still wants subsystems inside the reflection world, that is an
ARCHITECTURE decision (Subsystem joining the Object/RTTI hierarchy) worth its own
ideas-doc evaluation - not a facade-cleanup side effect.

### What P0 shipped anyway (the audit's actionable findings)

- **The physics statefulness fix** (the one unambiguous win, sanctioned regardless):
  `ScenePhysics.rayCast` now returns an EXPLICIT `RayCastHit` value handle
  (hit/distance/position/normal/surface + entity() + impulse()); the stored
  lastHit/SetLastHit state and the eight stateful accessors (hitX/Y/Z, hitNormal*,
  hitSurface, rayHitEntity, impulseOnHit) are DELETED from PhysicsSceneSystem and the
  facade. Registered + prelude-visible; tests rewritten to the explicit shape.

### Phases P1-P4: NOT BUILT, pending the user's call on the verdict

If the user accepts the recommendation, this spec closes here (P0 + the physics fix =
the deliverable) and the remaining genuine item - the run coordinator / tier-handle
ergonomics - stays with the shipped `run` facade. If the user overrules, P1 begins with
the Subsystem-RTTI architecture evaluation as its own gated design.

### CLOSED (2026-08-22): user accepted the verdict

The spec closes with P0 + the RayCastHit fix as its deliverable. The five statics
(Audio, Input, Net, run, ui) stand as the curated script-view layer; no both-patterns
state ever existed. Any future subsystem-reflection ambition starts from a fresh
evaluation with the blocking facts above as its opening constraints.

### P4 addendum (2026-08-22): both P4 halves closed explicitly, on separate grounds

The user flagged that the blanket closure swallowed P4's run item under a verdict that
did not technically cover it. Recording the two halves separately:

- **Net -> `NetworkController.of(context)`: closed BY the audit verdict.** It rides the
  same Context-handle machinery the verdict rejected (blocking fact 3), and
  post-networking-extraction the `Net` facade is already a thin curated view over
  INetworkController - the convergence the ideas doc wanted has happened.
- **run -> injected coordinator handle: closed on its OWN grounds (user accepted).**
  This half needed no subsystem reflection - the handle would be a value facade like
  Scene/Entity, fully inside the reflection world. It closes because the static `run`
  facade already IS the coordinator surface in substance: a reserved name resolving a
  per-context service through CurrentScriptContext, so each GameInstance's scripts
  reach their own coordinator (the multi-instance correctness injection was meant to
  buy is already present). The migration would change only the delivery mechanism and
  the spelling, at the cost of churning a days-old PaperKid-load-bearing surface built
  to the documented statics-take-parens convention, plus needing the per-backend
  base-class sugar the ideas doc itself ruled must never be load-bearing.
  REOPEN TRIGGER: the Game tier growing a genuine second coordinator-shaped need
  (e.g. multiple concurrent run scopes visible to one script).

Consequence of run-as-static being the product surface: EVERY host must register it.
The user's export test hit exactly this - `run::` unresolved in the exported game
(weekly 2026-08-22 item). Diagnosed: Engine.DefaultApp keeps a hand-rolled facade
registration list (DefaultApplicationImpl.cpp) that omits RegisterRunScriptFacade,
while editor/cook/MCP call the engine::RegisterAllScriptFacades root (which includes
it). Fix direction: DefaultApp registers via the ScriptSurface root instead of its
drifted parallel list, putting the exported player under the same count tripwire.
