# Entity active state: inactive stops BOTH rendering and simulating  (archived)

> Status: ARCHIVED - fully built. Non-authoritative: this is the original build spec, kept
> as the record of what was built and why; present-tense truth is the code + tests.

Status: SPEC (Fable, 2026-08-17). Ready to build. Origin: week-2026-08-15 item +
Opus's correct assessment that it is NOT a quick win - no system honors
`OnEntityActiveChanged` and the render extractor never checks `IsActive`.

## What exists today (verified against code, 2026-08-17)

The AUTHORING side is complete end-to-end; the CONSUMPTION side is zero:

- `Scene::IsActive/SetActive` (Scene.cppm / SceneImpl.cpp): one bool per entity
  record, default active. `SetActive` early-outs on no-change, bumps the scene
  revision, and notifies every system via `SceneSystem::OnEntityActiveChanged`.
- **No system overrides `OnEntityActiveChanged`** (repo-wide grep: zero hits
  outside the declaration + call site).
- **Render extraction never checks `IsActive`**: every `Extract*Into` loop in
  Engine.Render/ExtractImpl.cpp (meshes, instanced meshes, sprites, decals,
  cameras, lights, probes) iterates `manager->ForEach(...)` unconditionally.
- The flag IS authored and persisted: the inspector has an undoable Active
  checkbox (`SetActiveCommand`, EditContext.cppm ~990), duplicate/prefab paths
  carry it, and the scene wire serializes `active` per entity
  (SceneResourceImpl.cpp ~136/~160, prefab payloads ~751). Toggling it today
  changes NOTHING at runtime.
- The flag is NON-hierarchical: `SetActive` flips one entity; children are
  untouched and there is no effective-active concept.
- **Load-order trap**: `LoadScene` reads the entities block (id, name, active,
  parent, transform) and calls `SetActive` BEFORE any components are read. An
  `OnEntityActiveChanged`-driven design would fire when there is nothing to
  gate yet. This is the structural reason the event hook is NOT the mechanism.

## Semantics (the contract)

Unity/Godot-shaped, because it is what users expect:

1. **Own flag vs effective state.** `IsActive(e)` stays the entity's OWN flag
   (authored, serialized - unchanged wire, no DataVersion bump). NEW:
   `IsEffectivelyActive(e)` = own flag AND every ancestor's own flag. All
   runtime gating uses EFFECTIVE state; deactivating a parent deactivates the
   whole subtree without touching the children's own flags (re-activating the
   parent restores exactly the children that were themselves active).
2. **Inactive means dark.** An effectively-inactive entity does not render
   (mesh/instanced/sprite/decal/light/probe/camera), does not simulate
   (physics body out of the world, no script updates, no animation advance, no
   particle sim/emission, no audio voices), does not net-replicate, and its
   game-UI does not show. Transforms still resolve (`GetWorldMatrix` works) so
   editor gizmos, reparenting, and programmatic reads stay correct and cheap.
3. **Scene-starts-inactive is first-class** (user requirement 2026-08-17): an
   entity whose serialized flag is false must never have existed to the
   runtime domains - no body created, no voice started, no `onStart`, never
   extracted - from the very first frame after load. The design below gets
   this FOR FREE because every domain evaluates state in its own tick rather
   than reacting to a change event (there is no "change" at load to react to).
4. **Scripts lifecycle (v1 rules).** Effectively-inactive entities get no
   `onStart` / `onUpdate` / `onFixedUpdate` and coroutines are not resumed. An
   entity that has never started fires `onStart` on its FIRST EFFECTIVELY
   ACTIVE tick (so spawn-disabled-then-enable behaves like Unity). An already
   started entity that deactivates simply stops receiving updates and resumes
   on reactivation - **no `onEnable`/`onDisable` events in v1** (deferred;
   the overloaded-name/event-bus surface can add them later without breaking
   these rules). `onStop` on destroy/scene-stop fires regardless of active
   state (cleanup must run).
5. **Physics state across deactivate/reactivate (v1).** Deactivation DESTROYS
   the Jolt body (`c.body` invalidated); reactivation lazily re-creates it via
   the existing create-when-invalid path in the physics tick. Velocities are
   therefore LOST across a toggle - documented v1 behavior. (v2 option if it
   ever matters: `PhysicsWorld` grows remove-from-world/add-to-world so the
   JPH body object survives; do not build it speculatively.) Rationale:
   skipping the sync loop is NOT enough - Jolt steps every body in its world
   internally, so an inactive entity's body would keep falling.
6. **Non-goals (v1)**: per-COMPONENT enabled flags; an editor-only "hidden"
   flag distinct from runtime active; ghost/dimmed rendering of inactive
   entities in the editor viewport (they simply vanish, like Unity/Godot);
   seeding Jolt's sleep state from the flag.

## Design

### P1 - the effective-active cache (foundation.scene)

The one new mechanism everything else consumes:

- Add an `effectiveActive` bit to the entity record next to `active`.
- `IsEffectivelyActive(EntityHandle)` = O(1) read of the cached bit.
- Recompute by subtree walk (GetFirstChild/GetNextSibling exist) at the ONLY
  two choke points that can change it:
  - `SetActive(e, ...)`: recompute e's subtree (parent chain read once for
    e's own ancestor state, then propagate down; children whose own flag is
    false terminate that branch early - their subtrees are already false).
  - `SetParent(child, ...)` (both overloads + the sibling-insert variant):
    recompute child's subtree against the new parent's effective state.
- Entity creation: `effectiveActive` = own default (true) AND the spawn
  parent's effective state.
- `OnEntityActiveChanged` stays exactly as-is (own-flag change notification,
  useful for editor listeners) and is explicitly documented as NOT the gating
  mechanism. Do not add a per-descendant notification storm.
- Scene load needs no special code: `SetActive(h, false)` during the entities
  block recomputes the (component-less) subtree; components arriving later
  poll the already-correct bit on their first tick.

Tests (foundation Scene.Tests): deep-chain effective state; parent-off /
child-own-flag-on matrix; reparent an active entity under an inactive parent
(and out again); create-under-inactive-parent; serialize round-trip preserves
own flags and reload yields the same effective states.

### P2 - render extraction gates (Engine.Render)

One branch at the top of every `ForEach` lambda in ExtractImpl.cpp:
`if (!scene.IsActive-effective(e)) return;` for meshes, sprites, decals,
lights, reflection probes, and cameras (`ExtractPrimaryCamera` must skip
inactive cameras so the primary-camera pick falls through to the next one).
Environment/post-process settings systems are scene-level, not entity-level -
no gate.

CAVEAT - instanced meshes: `ExtractInstancedMeshesInto` feeds the persistent
per-set GPU buffer path (O(1)/frame CPU by design). A per-entity gate must
participate in that path's set-version/dirty tracking so a toggle actually
rebuilds the persistent buffer; an extraction-side skip alone may be invisible
until an unrelated rebuild. Toggling active on an instanced-mesh entity is the
test that catches this.

Tests: extraction-count tests per component kind (build a scene, extract,
deactivate, extract again, counts drop; reactivate, counts restore; inactive
PARENT hides the child's mesh). Primary-camera fallback test.

### P3 - simulation gates, domain by domain (each its own commit)

The shared pattern for stateful domains is a PER-COMPONENT LATCH inside the
domain's own tick, not the change event: compare this tick's effective state
against the state the domain last acted on, and reconcile. It is
order-independent, self-healing, immune to the load-order trap, and costs one
branch per component per tick.

- **Physics** (Engine.Physics, 5 systems): in the body sync loop - inactive
  with a valid body -> `DestroyBody` + invalidate `c.body` (+ character
  equivalent); inactive without a body -> skip creation (the existing lazy
  create-when-invalid already re-creates on reactivation). Contact dispatch
  already tolerates stale bodies ("delivered as an invalid handle"). Joints
  referencing a destroyed body: verify the existing joint teardown path
  handles it (same code path as destroying a body's entity).
- **Scripts** (Engine.Script, 3 systems): gate the behavior update walk on
  effective state per the v1 lifecycle rules above (started-flag stays false
  until the first active tick; coroutine resume respects the gate). Level/
  scene-tier scripts are scene-level - no gate.
- **Animation** (Engine.Animation, 4 systems incl. PropertyAnimator): skip
  advance + apply for inactive entities (`OnUpdate`'s `ForEach` gate). Time
  does NOT advance while inactive (v1; matches "does not simulate").
- **Particles** (Engine.Particles, 1 system): inactive -> no sim step, no
  emission; existing particles FREEZE (v1 - cheapest and reversible;
  clear-on-deactivate can become a per-component option later if wanted).
- **Audio** (Engine.Audio, 4 systems): inactive -> stop the entity's voices
  (the latch's deactivate edge); no voice starts while inactive.
- **Game UI** (Engine.UI, 3 systems): inactive entity's canvas/overlay does
  not update or render.
- **Net** (foundation.net, 2 managers): inactive entities drop out of
  replication extraction (mirror of render). Flag-state itself replicating is
  a networking-track question - out of scope here; note it in the net spec.

Tests per domain in that domain's Tests target: the load-starts-inactive case
(load a scene with an inactive entity carrying the domain's component; assert
nothing happened: no body, no `onStart`, no voice, no particles) AND the
runtime-toggle case (activate -> comes up exactly once; deactivate -> goes
dark; reactivate -> resumes; scripts: `onStart` fired exactly once total).

### P4 - surfaces (after the engine behaves)

- **Script facade**: `entity.active` (get/set own flag) +
  `entity.activeInHierarchy` (get effective) on the entity facade - currently
  absent entirely. Natural C++ types per the facade rules.
- **Editor hierarchy**: dim inactive entities' rows (own-flag off = dim;
  effectively-inactive via ancestor = dimmer or italic - pick one, keep it
  cheap). The inspector checkbox already exists and needs nothing.
- **MCP**: scene tools should report `active` on entities if they don't
  already; cheap while in there.

## Build order + effort

P1 (foundation cache + tests) -> P2 (render gates; THE visible win, small) ->
P3 physics -> P3 scripts -> P3 animation/particles/audio/UI/net (mechanical
after the first two establish the latch pattern) -> P4 surfaces. Each step
lands battery-green on both compilers with tests, per the standing rule. P1+P2
is roughly a session; each P3 domain is small but must be verified in-editor
(Simulate on) not just in tests.

## Explicitly decided (do not relitigate without new evidence)

- Effective state is CACHED on the entity record, recomputed at the two choke
  points - not computed by parent-walk per query (extraction would walk per
  entity per frame), not event-fanned to systems (load-order trap).
- Consumers POLL the bit in their own loops; only stateful domains add the
  latch for edges. `OnEntityActiveChanged` gates nothing.
- v1 physics toggle loses velocities (destroy/recreate). Documented, cheap,
  uses the existing lazy-create path.
- No script enable/disable events in v1; `onStart` waits for the first
  effectively-active tick.
- Wire format unchanged: `active` (own flag) is already serialized everywhere
  it needs to be.

## BUILT (Fable, 2026-08-18) - all four phases, battery-green clang+gcc

Execution notes + deviations discovered against the code (each deliberate):

- **P1 as specced.** `EntitySlot.effectiveActive` + `IsEffectivelyActive` (O(1)),
  subtree resettle at SetActive / SetParent / MoveBefore / creation, with the
  own-flag-false early-out. Destroy needs nothing (subtrees are destroyed, never
  reparented). Loader order (SetActive during the entities block, parents relinked
  after) is handled BY the SetParent choke point - verified by the
  saved-inactive-parent/active-child round-trip test.
- **P2 as specced** (8 loops incl. both mesh paths + primary-camera fallthrough).
  The instanced-mesh caveat DISSOLVED: the renderer draws from the extracted
  snapshot, so absence = not drawn; the persistent buffer is only a cache and
  revalidates by version on return. Particles' render extraction (the provider in
  engine.particles) is gated too.
- **P3 physics**: per-entity create helpers (CreateBodyForEntity / ...Character /
  ...Joint) shared by scene-start (skips inactive, latches simActive) and a
  ReconcileActiveState() edge pass at the top of OnFixedUpdate. Joints reconcile on
  the full want/have compare - an ACTIVE entity's joint drops when its explicit
  TARGET deactivates and rebuilds when it returns (silent create path; scene-start
  keeps the loud warnings). SIDE IMPROVEMENT: a rigid body component ADDED mid-run
  now gets a body on the next activation edge (previously bodies were built only at
  scene start). v1 toggle = destroy/re-create at the CURRENT pose, momentum cleared.
- **P3 scripts - one ruling refined**: per-BEHAVIOR onEnable/onDisable machinery
  already existed (behavior.enabled). The entity gate does NOT route through it:
  entity-inactive freezes silently (no events, per the v1 ruling), cancels pending
  coroutines on the edge (the existing disabled-behavior semantic - the scheduler
  has no per-behavior pause, so "not resumed" v1 = cancelled), and onStart waits
  for the first effectively-active tick. Bus events + entity.send/contact delivery
  skip inactive targets. v2 MAY route entity edges through onEnable/onDisable -
  the machinery is there.
- **P3 animation/particles**: freeze as specced (time does not advance; particles
  also stop RENDERING while frozen).
- **P3 audio**: PlayComponent refuses inactive entities; the sync loop stops voices
  on the deactivate edge and restarts AUTOPLAY sources on reactivate
  (activeSuspended latch, armed at scene start for saved-inactive autoplay).
- **P3 UI**: gated at the visibility level (canvas root Gone in build pass +
  UpdateSceneView, billboards Gone, panel sprite hidden + RT draw skipped + pointer
  ray-routing skips inactive panels). The RT texture object survives (a cache).
- **P3 net - deviation from the one-line spec sketch**: inactive entities do NOT
  drop out of snapshots (the wire is spawn-based; absence would mean despawn).
  Instead their replicated STATE freezes: capture/apply/interpolation-sampling skip
  them; identity + existence stay on the wire. Replicating the flag itself remains
  the networking track's question, as originally noted.
- **P4**: entity.active() / setActive(bool) / activeInHierarchy() on the script
  Entity facade (both backends, tested from Wren); hierarchy rows dim to 45% alpha
  when effectively inactive (prefab-blue dims too); MCP needs nothing - scene_read
  serves the scene document, which already carries per-entity `active`.

Tests (all green both compilers): Scene.Tests effective-active suite (5 cases),
Scene.Resource.Tests load-inactive round-trips, Engine.Render.Tests extraction
gates + camera fallthrough, Engine.Physics.Tests (starts-inactive / toggle /
joint-target), Engine.Script.Tests (starts-inactive onStart deferral / silent
freeze / facade), Engine.Animation.Tests property-animator freeze,
Engine.Particles.Tests attach+freeze, Engine.Audio.Tests voice stop/restart,
Net.Replication.Tests state freeze + stays-in-snapshot, Engine.UI.Tests canvas
Gone/back via an inactive parent.

Remaining (documented, not built): editor-viewport ghost rendering (non-goal),
per-component enabled flags (non-goal), physics velocity preservation across a
toggle (v2 option), script onEnable/onDisable on entity edges (v2 option),
active-flag replication (networking track).
