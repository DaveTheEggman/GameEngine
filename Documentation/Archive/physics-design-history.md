# Physics - design history (archived)

> Status: ARCHIVED
> Superseded by: Documentation/Systems/physics.md
> Track: [[physics-p1]]

NON-AUTHORITATIVE. The reference survey and design rationale behind the 2026-07 Jolt physics build.
Present-tense truth is `Systems/physics.md`; the full original design doc (goals, phasing P0-P3, the
detailed per-section design) is in git at the P0 commit 3b92560d. Kept for the "why".

## Reference survey - conclusions

**Sedulous** (`Sedulous.Physics*`): keep the slotmap handles and the lock-free contact buffer (entity
handles packed in body user data so decoding never re-enters Jolt). Avoid everything else: one fat
40-method interface, engine hard-bound to the Jolt type anyway, `JPH_Init` per world (it is global),
O(n) BodyID->handle reverse scan on every callback, faked ray normals, hardcoded 2-layer filtering
that ignored the descriptor's layer field, ignored solver-steps settings, `ReleaseShape` violating
its own refcount contract, dead proxy layer, orphaned character API, no overlap queries, no
interpolation, no assets, debug draw commented out.

**ezEngine** (`JoltPlugin`) - the strongest overall reference: body + shape component split
(`GatherShapes` walks the hierarchy into a `StaticCompoundShape`, stopping at nested bodies);
two-level ObjectLayer encoding (designer NxN matrix in the low byte x hard-coded semantic broadphase
table in the high byte); buffered worker-thread listeners -> deferred main-thread delivery;
`SemiFixed` stepping (coalesce equal steps into one `Update(dt, n)`); offline-cooked meshes; job
system on the engine task system; chunked temp allocator; batch add + `OptimizeBroadPhase`. Gap: no
render interpolation; `GetGravity()` returns a hardcoded constant.

**Godot** (`modules/jolt_physics`): dynamic (layer,mask)->ObjectLayer interning (the alternative to a
fixed matrix); linear temp allocator with graceful fallback + warning; explicit warnings on Jolt
update-error bits; ~40 exposed Jolt tuning knobs (the settings checklist); zero global gravity with
per-body accumulation (enables gravity volumes); double-precision option. Gaps: runtime-only shape
builds, one collision step per tick.

**Flax** (PhysX; asset story only): `CollisionData` binary asset (cooked convex OR trimesh, padded
versioned header, source-model GUID + LOD + vertex limit, face-remap table mapping cooked-hit face ->
source triangle for surface-on-hit); `PhysicalMaterial` asset (friction/restitution each with a
combine-mode override, density, gameplay `Tag`); per-actor collision delegates as the script-friendly
event surface. Pitfall: async transform flush forbids nested rigid bodies.

## Key design calls

- **No abstraction theater.** `foundation.physics` wraps Jolt directly - thin engine types on the API
  (`BodyHandle`, `RayHit`, `ShapeRef`) but NO `IPhysicsWorld` interface a second backend would
  implement. Sedulous and ez both proved that seam goes unused and gets hard-bound anyway. Jolt types
  never leak above `foundation.physics`.
- **Global init once**, not per world (Sedulous's per-world `JPH_Init` breaks multi-scene). One
  `PhysicsWorld` per scene (matches audio's per-scene grouping and play-in-editor teardown); worlds
  are created lazily on first simulation start so idle editor scene pages pay nothing.
- **Reverse mapping** `BodyID` -> dense index via body user data (we control creation) - O(1), fixing
  the Sedulous linear scan.
- **Layer model**: ez's fixed named-group matrix chosen over Godot (layer,mask) interning. Revisit
  if games need per-body mask exceptions; Jolt's group filters cover the "joint pair should not
  collide" case regardless.
- **Interpolation** (our improvement over all four surveyed engines): components store prev/curr
  poses; render frames write `lerp`/`Slerp` at the accumulator alpha, so gameplay reads what players
  see. Open question at design time (transform-change churn from writing every render frame) resolved
  by shipping scene-transform interpolation and measuring; the render-extraction fallback stays
  available if it ever profiles hot.
- **Job system**: v1 = Jolt's own `JobSystemThreadPool`; swap in a `JobSystemWithBarrier` adapter
  onto the engine task system when the worker seam stabilizes (see backlog: per-world pool
  consolidation).

## Deviations from the original plan (and why)

- ONE `ColliderComponent` with a `ShapeKind`, not per-primitive shape components - simpler component
  set, same compounding behavior.
- ONE `JointComponent` with a `JointKind` enum, not five component types; nil target = nearest
  ancestor body.
- Collision groups live in SCENE settings (a `PhysicsSettings` scene component), not project settings
  - consistent with gravity living with the scene.
- A `Plane` shape kind was added (not in the original primitive list).
