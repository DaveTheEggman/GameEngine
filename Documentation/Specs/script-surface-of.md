# Script surface: retire the facade layer via `.of` on reflected real types

Status: SPEC - ready to build AFTER networking-extraction.md and after PaperKid P0 has
proven the current surface (the sequencing ruling in scripting-runtime-shape.md).
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
