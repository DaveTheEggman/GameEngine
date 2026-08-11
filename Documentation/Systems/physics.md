# Draconic Physics — Jolt-backed subsystem (design)

Status: P1 + P2 + P3 SHIPPED (P1 c5703e7, P2 387f722+cff7c0b, P3 049bdd5+4909a1e,
2026-07-18). P3 = joints with motors (Fixed/Point/Hinge/Slider/Distance; nil target =
nearest ancestor body), CharacterVirtual component (+ Wren control), 32 designer collision
groups (semantic<<8|group ObjectLayer encoding, per-scene matrix + names, query masks) and
the inspector matrix grid editor. Deviations: ONE JointComponent with a kind enum (not five
component types); groups live in SCENE settings (not project settings - consistent with
gravity); plane shape added (not in the original plan). Parked items: see §10.

## 1. Goals

- Rigid bodies, triggers, queries, and (phase 2+) characters/joints in our value-pool
  component style, with a **real collision-layer system** and **real query results** —
  Sedulous shipped fake ray normals, stubbed contact data, and a layer API the backend
  ignored; those are non-negotiable fixes.
- **Collision shapes and physical materials as assets**: cooked convex/mesh shapes from
  render meshes (none of Sedulous's inline-primitives-only), physical materials with
  gameplay tags.
- Fixed-timestep stepping **with render interpolation** — none of the four surveyed engines
  interpolate; all stutter when render FPS ≠ tick rate. We do better here.
- Worker-thread contact events delivered safely on the main thread (the one thing Sedulous
  got right — keep and refine).

## 2. Reference survey — conclusions

**Sedulous** (`Sedulous.Physics*`): keep the slotmap handles and the lock-free contact
buffer (entity handles packed in body user data so decoding never re-enters Jolt). Avoid
everything else: one fat 40-method interface, engine hard-bound to the Jolt type anyway,
`JPH_Init` per world (it's global), O(n) BodyID→handle reverse scan on every callback, faked
ray normals, hardcoded 2-layer filtering that ignored the descriptor's layer field, ignored
solver-steps settings, `ReleaseShape` violating its own refcount contract, dead proxy layer,
orphaned character API, no overlap queries, no interpolation, no assets, debug draw
commented out.

**ezEngine** (`JoltPlugin`) — the strongest overall reference:
- **Body + shape component split**: one actor component, N shape components on the entity
  or children; `GatherShapes` walks the hierarchy into a `StaticCompoundShape`, stopping at
  nested bodies; single shapes skip the compound. Kinematic = a flag on the dynamic body.
- **Two-level ObjectLayer encoding**: designer-configured named-group NxN matrix in the low
  byte (`CollisionLayers.cfg` project config + editor dialog) × hard-coded semantic
  broadphase table in the high byte (Static/Dynamic/Query/Trigger/Character/Ragdoll/Debris,
  with one-way rules like Debris). Static asserts keep enums in sync.
- Buffered worker-thread listeners → deferred delivery in a named main-thread phase;
  `SemiFixed` stepping (fixed steps, stretch when behind, coalesce equal steps into one
  `Update(dt, n)`); collision meshes cooked offline via `SaveBinaryState`; job system
  adapted onto the engine task system; growing chunked temp allocator; batch body add +
  `OptimizeBroadPhase` threshold; weight/impulse category configs; separate character
  gravity. Z-up, meters.
- Gap: no render interpolation; `GetGravity()` returns a hardcoded constant.

**Godot** (`modules/jolt_physics`):
- **Dynamic (layer,mask)→ObjectLayer interning** (hash the pair, allocate object layers on
  demand, symmetric `mask1&layer2 || mask2&layer1` test) — the alternative to a fixed
  matrix; `BODY_STATIC_BIG` broadphase tree for huge statics; linear temp allocator with
  graceful fallback + warning; explicit warnings on Jolt update-error bits; **~40 exposed
  Jolt tuning knobs** (the checklist for our settings block); zero global gravity with
  per-body gravity accumulation (enables gravity volumes); double-precision option.
- Gaps: runtime-only shape builds (cook cost at load), one collision step per tick.

**Flax** (PhysX; asset story only):
- **`CollisionData` binary asset**: cooked convex OR trimesh, fixed padded 128-byte header
  (versioning), source-model GUID + LOD + vertex limit + material-slots exclusion mask, and
  a **face-remap table** mapping cooked-hit face → source mesh triangle (surface-on-hit for
  footsteps/decals). Optional runtime cooking behind a settings flag.
- **`PhysicalMaterial` asset**: friction/restitution each with per-material combine-mode
  override, density, and a gameplay `Tag`. Per-actor collision delegates are the most
  script-friendly event surface. Pitfall: async transform flush forbids nested rigid
  bodies; static-vs-dynamic implicit in hierarchy (silent static actor creation).

## 3. Architecture

```
ThirdParty/Jolt              vendored
draconic.physics             Jolt wrapper: PhysicsWorld, bodies/shapes/queries/filters,
                             layers, contact buffering, temp alloc, job adapter
draconic.physics.resource    CollisionShape + PhysicalMaterial cooked resources + factories
draconic.physics.editor      collision/material assets + builders (+ import integration)
draconic.physics.subsystem   PhysicsSubsystem + components + fixed-step + interpolation
```

- **No abstraction theater.** `draconic.physics` wraps Jolt directly (thin engine types on
  the API surface — `BodyHandle`, `RayHit`, `ShapeRef` — but no `IPhysicsWorld` interface a
  second backend would implement; Sedulous and ez both proved the seam goes unused and gets
  hard-bound anyway). Jolt types never leak above `draconic.physics`.
- **Global init once** (`JPH::RegisterTypes`, factory, allocator hooks routed through our
  allocator) in a module-level `PhysicsCore`, not per world — Sedulous's per-world
  `JPH_Init` breaks multi-scene. One `PhysicsWorld` per scene (matches audio's per-scene
  grouping and play-in-editor teardown).
- **Job system**: v1 = Jolt's own `JobSystemThreadPool` (correct, simple). When the engine
  task system lands (renderer §13 task-graph work), swap in a `JobSystemWithBarrier`
  adapter (ez/Godot both show the shape). Temp allocator: linear block with graceful
  heap fallback + warning (Godot).
- **Reverse mapping**: `JPH::BodyID` → dense index via body user data (we control creation)
  — O(1), fixing the Sedulous linear scan on every callback and hit.

### 3.1 Components

- **`RigidBodyComponent`** (serializable): `motionType` (Static/Dynamic + `kinematic`
  flag), `layer` (named collision group), `mass` (0 = computed), `friction`, `restitution`,
  `linearDamping`, `angularDamping`, `gravityFactor`, `isSensor`, `allowSleep`, `ccd`
  (LinearCast), DOF locks (incl. a 2D preset). Runtime control surface (Sedulous gap):
  `SetLinearVelocity/AddForce/AddImpulse[AtPosition]/AddTorque/Teleport(pos,rot)` —
  teleport does a hard `SetPositionAndRotation` + broadphase-safe activation.
- **Shape components** (each serializable, compounded ez-style by hierarchy walk that stops
  at nested bodies): `BoxShapeComponent`, `SphereShapeComponent`, `CapsuleShapeComponent`,
  `CylinderShapeComponent`, `ConvexShapeComponent` (Ref<CollisionShape>),
  `MeshShapeComponent` (Ref<CollisionShape>; static/kinematic only), each with local
  offset/rotation + `material` (Ref<PhysicalMaterial>). Non-unit scale wraps in
  `ScaledShape`; the compound is rebuilt when any shape component dirties.
- **`CharacterComponent`** (phase 3): Jolt **CharacterVirtual** (not the simple character
  Sedulous wired and never used) — capsule params, max slope, step up/down, ground state
  query, move API. ez's warning applies: ship it as a solid default, expect games to
  customize.
- Joints (phase 3): `FixedJoint/HingeJoint/SliderJoint/DistanceJoint/PointJoint` components
  referencing a second entity by guid; motors actually applied (Sedulous copied limits
  only).

### 3.2 Stepping, sync, interpolation

- **Prerequisite (P0)**: a fixed-update lane. The runtime tick gains an accumulator
  (`fixedStep = 1/60`, `maxSteps = 4`, spiral-of-death clamp) driving a new
  `Scene::FixedUpdate` pass; `ScenePhase` itself is untouched (fixed update runs before the
  variable phases each frame, 0..N times).
- Per fixed step: (1) kinematic bodies ← scene transforms via `MoveKinematic` (velocity-
  correct); (2) coalesced `PhysicsSystem::Update` (ez SemiFixed policy); (3) drain contact
  buffer → dispatch events; (4) dynamic poses → component `currentPose` (prev saved first).
- **Interpolation (our improvement)**: components store `prevPose`/`currentPose`; every
  RENDER frame, the subsystem writes scene transforms as
  `lerp(prevPose, currentPose, accumulatorAlpha)` (position lerp + quaternion slerp),
  scale untouched. Gameplay reading transforms sees the smoothed value — documented,
  consistent, and what players see. `Teleport` snaps both poses (no ghost lerp).
- Transform ownership: dynamic = physics owns pos/rot (scene edits to dynamic bodies while
  simulating are ignored — use `Teleport`); kinematic = scene owns; static = immutable
  while simulating (editor edits outside simulation rebuild on play, the existing
  `OnSceneStarted` snap pattern).

### 3.3 Layers & filtering

ez's two-level model, adapted:
- **Broadphase layers** (fixed, semantic, hard-coded table): `Static`, `Dynamic`, `Query`,
  `Trigger`, `Character`, `Debris` (one-way: collides with Static/Dynamic, nothing collides
  with it).
- **Collision groups** (designer-facing): up to 32 NAMED groups + NxN matrix, stored in
  **project settings** (a reflected `PhysicsSettings` settings section — we already have
  the typed settings store; no bespoke .cfg). ObjectLayer = (broadphase << 8) | group.
- Queries take a group **mask** + optional per-body ignore filter; the mask is actually
  applied (Sedulous's never was).

### 3.4 Queries & events

- `RayCast/RayCastAll`, `SweepSphere/Box/Capsule`, `OverlapSphere/Box/Capsule` (boolean +
  collect variants — overlap was entirely missing in Sedulous). Hits carry REAL surface
  normals, body handle, entity guid, distance/fraction, and — for cooked mesh shapes — the
  face-remap index resolving to the source-mesh triangle + its physical material tag
  (Flax; enables footsteps/impact effects).
- **Contact/trigger events**: Jolt worker-thread listeners append to a fixed lock-free
  buffer (`Interlocked` cursor, entity guids packed in user data — the good Sedulous
  bones), with an overflow COUNTER surfaced as a warning (not silent drop). Main-thread
  dispatch after the step: per-component `Function<>` callbacks
  (`OnContactBegin/End`, `OnTriggerEnter/Exit` with entity + point + normal + impulse) and
  later Wren events. Trigger enter/exit tracked by body-pair refcount (ez).
- Contact data is REAL: relative velocity and combined friction/restitution from Jolt's
  manifold (Sedulous hardcoded these).

## 4. Runtime resources

- **`CollisionShape`** (cooked product): header {type: ConvexHull | TriangleMesh |
  ConvexDecomposition, source model guid, vertex limit, material-slot mask, version} +
  Jolt `SaveBinaryState` bytes + face-remap table (trimesh). Factory restores via Jolt
  stream-in; instances share the restored `ShapeRefC` (real refcounting — Jolt's, not a
  broken shim).
- **`PhysicalMaterial`** (cooked product): friction + combine mode, restitution + combine
  mode, density, `tag` (string, e.g. "wood"). Combine modes applied via Jolt's
  friction/restitution combine callbacks (Godot precedent).
- Both resolve through standard `resource::Ref` + the scene resolve pass; staged into paks
  like everything else. Player needs zero cooking code (runtime cooking is OFF by default;
  a `supportRuntimeCooking` escape hatch can come later, Flax-style).

## 5. Editor-side assets

- **`CollisionShapeAsset`** (source): references a source model/mesh asset by guid +
  settings {type, convex vertex limit [8..255], decomposition on/off, material-slot mask,
  min triangle area}. **Builder** cooks via Jolt (convex hull / triangle mesh /
  decomposition), records the dependency edge on the source model (recook cascades when the
  model reimports), emits shape bytes + face-remap.
- **`PhysicalMaterialAsset`**: plain reflected asset; New Asset menu entry; edited in the
  inspector.
- **Model import integration**: the model import dialog (P3 import-options work) gains a
  `Generate collision` toggle (+ convex/trimesh choice) — the importer emits a
  CollisionShapeAsset beside the meshes, and the generated **prefab** gets a
  `MeshShapeComponent`/`ConvexShapeComponent` + static `RigidBodyComponent` on the root
  when enabled. (Import → placeable, collidable prop in two clicks.)
- **Gizmos & debug draw**: shape components draw via the existing component-gizmo registry
  (box/sphere/capsule wires, convex/mesh outline from cached debug geometry on the cooked
  asset, Flax-style shared cache); a `Physics.DebugDraw` toggle routes Jolt's
  `JPH_DEBUG_RENDERER` output into our debug-draw layer (Sedulous left this commented out;
  we have a real debug-draw system to receive it).
- **Settings UI**: the `PhysicsSettings` section (gravity, fixed rate, solver iterations,
  Jolt limits — seeded from Godot's exposed-knob list) edits through the existing settings
  dialog; the collision-group matrix gets a dedicated grid editor in a later pass
  (reflection inspector renders names + masks in v1).

## 6. Scripting (Wren)

Phase 2+: `Physics.rayCast(from, dir, mask)` returning hit objects; body control on
entities (`entity.body.addImpulse(...)`, `teleport`); contact events routed to entity
script handlers. Registered via the standard reflection→script path.

## 7. Portability

Jolt compiles to WASM (no threads variant supported — job system falls back to
single-threaded in-line execution; the adapter seam covers it) and Android/ARM (NEON).
Determinism is explicitly NOT pursued (cross-platform determinism requires locked-down
math everywhere; revisit only if lockstep networking ever matters).

## 8. Phasing

- **P0 — fixed-update lane**: runtime accumulator + `Scene::FixedUpdate` + tests. (Small,
  independently useful.)
- **P1 — core sim**: `draconic.physics` (world/bodies/primitive shapes/layers/queries/
  events/temp-alloc/reverse-map), subsystem + RigidBody/primitive-shape components +
  hierarchy compounding + kinematic/dynamic sync + **interpolation**, PhysicsSettings
  section, debug draw, Sandbox proof (falling crates + trigger + raycast picking) + player.
  Tests: simulation determinism-enough unit tests (spawn/step/assert poses), compound
  building, layer matrix, query filtering, contact buffering (synthetic).
- **P2 — assets**: CollisionShape + PhysicalMaterial assets/builders/resources/factories,
  mesh/convex shape components, face-remap surface-on-hit, model-import collision option,
  gizmos polish, Wren queries + body control.
- **P3 — character + joints**: CharacterVirtual component, joint components with motors,
  collision-matrix grid editor, gravity volumes if wanted (zero-global-gravity approach,
  Godot).

## 9. Open questions

1. Interpolation writes scene transforms every render frame — transform-change churn for
   the dirty-propagation system. If profiling objects, fall back to interpolating only in
   render EXTRACTION (render-side pose override, scene transform updated at fixed rate).
   Recommendation: start with scene-transform interpolation (simpler, gameplay-consistent),
   measure.
2. Collision groups: fixed named matrix (ez) chosen over (layer,mask) interning (Godot) —
   revisit if games need per-body mask exceptions; Jolt's group filters cover the "joint
   pair shouldn't collide" case regardless.
3. Double-precision Jolt for large worlds — off by default; flag exists if an open-world
   project appears.
4. One `PhysicsWorld` per scene multiplies Jolt fixed costs per open editor scene page.
   Editor scenes don't simulate until play — lazily create the world on first simulation
   start. (Decision folded in; noting for review.)

## 10. Parked (post-P3) — deliberate, not forgotten

- **Convex decomposition** (concave mesh -> multiple hulls, so concave props can be
  DYNAMIC; today concave = triangle mesh = static-only, or a single hull that loses the
  concavity). Parked: needs a decomposition library (V-HACD is the standard) - a new
  vendoring decision of the same weight as Jolt itself - and no current content needs a
  dynamic concave body. Revisit when one does; the seam is the CollisionShapeAsset
  builder (a `Decompose` cook kind emitting a compound blob).
- **Gravity volumes** (per-region gravity via the zero-global-gravity approach, Godot).
  Parked: no game demand, and the building blocks already exist - per-scene gravity plus
  the character's caller-owned velocity recipe mean a gameplay-side implementation needs
  no engine changes. Promote to engine only if several games rebuild the same thing.
- **JPH_DEBUG_RENDERER wiring** (Jolt's internal debug output into our debug-draw layer).
  Parked: component-side debug draw already covers the practical cases (shape wires,
  cooked outlines, character capsule, plane grid); Jolt's adds constraint-internal
  visualization but requires compiling the vendored Jolt with an extra define (a build
  change for every consumer) for marginal gain. Revisit if joint debugging gets painful.
- **Per-world job-pool consolidation.** Every PhysicsWorld constructs its own
  15-thread Jolt JobSystemThreadPool, and every STARTED scene constructs a world -
  observed during the 2026-07-18 Simulate-stop investigation (where Jolt was
  exonerated). Fine at today's 1-2 started scenes; a many-scenes editor session or a
  streaming game would want one shared pool (Jolt supports an external JobSystem -
  the engine job system is the natural donor once its worker seam stabilizes).
  Parked: no observed cost yet; revisit when scene counts grow.
- **Per-entity character/body scripting** (`entity.body.addImpulse(...)`-style Wren APIs).
  BLOCKED, not just deferred: Wren scripts have no entity handles yet. The current
  facade's "scene's first character" addressing is the stopgap for the single-player
  case. Unblocks when entity-level scripting lands (scripting design doc); the contact
  events -> entity script handlers item from §6 rides the same dependency.
