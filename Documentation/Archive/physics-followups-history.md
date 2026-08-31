# Physics - parked follow-ups

> ARCHIVED 2026-09-01: still-open items live in Documentation/Plans/week-2026-09-05.md
> ("Backlog folder absorbed"). This archive keeps the full detail.


> Status: CURRENT
> Track: [[physics-p1]]

Deliberately parked after P3 (not forgotten, not gaps). The shipped stack
(`Documentation/Systems/physics.md`) is complete for the current target; each item is a scoped
addition with a known seam.

- **Convex decomposition** (V-HACD). Concave mesh -> multiple hulls so concave props can be DYNAMIC;
  today concave = triangle mesh = static-only, or a single hull that loses concavity. Cook kinds are
  `ConvexHull` + `TriangleMesh` only. Parked: needs a decomposition library (a vendoring decision of
  Jolt's weight) and no current content needs a dynamic concave body. Seam: a `Decompose` cook kind on
  the collision-shape builder emitting a compound blob.

- **Gravity volumes** (per-region gravity, Godot's zero-global-gravity approach). Parked: no game
  demand, and the pieces exist already - per-scene gravity plus the character's caller-owned velocity
  mean a gameplay-side implementation needs no engine changes. Promote only if several games rebuild
  the same thing.

- **`JPH_DEBUG_RENDERER` wiring** (Jolt's internal constraint/contact visualization into our
  debug-draw layer). Parked: the `debugDraw` scene toggle + component-side shape wires already cover
  the practical cases; Jolt's adds constraint-internal detail but requires compiling vendored Jolt
  with an extra define (a build change for every consumer). Revisit if joint debugging gets painful.

- **Per-world job-pool consolidation.** Every `PhysicsWorld` builds its own Jolt
  `JobSystemThreadPool`, and every STARTED scene builds a world (observed during the 2026-07-18
  Simulate-stop investigation, where Jolt was exonerated). Fine at today's 1-2 started scenes; a
  many-scenes editor session or a streaming game would want one shared pool. Jolt supports an external
  `JobSystem` - the engine job system is the natural donor once its worker seam stabilizes.

## Resolved since the original parked list

- **Per-entity character/body scripting** is no longer blocked. `CharacterComponent` exposes reflected
  `move`/`jump`, and world-context physics ops are scriptable via `ScenePhysics.of(scene)` (rayCast +
  `applyImpulse(entity, ...)`). What remains future is routing contact events into per-entity script
  handlers.
