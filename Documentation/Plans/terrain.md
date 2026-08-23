# Terrain

**Status:** SPEC, ready to build (2026-08-10). Third of the three parity P0
tracks (after property-animation.md and navigation.md - see
docs/design/parity-2026-08.md). References: Lumix terrain (heightmap +
splat + grass) and Traktor's richer brush architecture
(elevate/flatten/smooth/cut/color/attribute brushes + align-to-terrain
operators) for the phase-2 editor.

GOAL: heightmap terrain that renders on every backend including web, has a
physics presence, and is authorable in-editor - phased so the runtime slice
ships before the sculpting experience.

USER DECISIONS (2026-08-10): rendering = CHUNKED GEO-MIPMAPPING, one code
path everywhere (WebGPU has no tessellation shaders, so Lumix-style GPU
tessellation is off the table by the one-path rule). Grass/vegetation =
phase 3 of THIS track. Ocean/river/forest = not planned.

## Module layout (confirmed shape)

Heightfield is a FIRST-CLASS ASSET below terrain, not embedded in it
(2026-08-22 decision, revised same day after auditing Traktor's
`Heightfield/Editor`: it has a full HeightfieldAsset -> HeightfieldPipeline
-> Heightfield resource chain, and Terrain/physics/undergrowth all REFERENCE
`resource::Id<Heightfield>` rather than embedding a heightmap). This is
exactly our `foundation.X` + `X.Resource` + `X.Pipeline` + `Editor.X`
convention (as textures/meshes get), and it makes the grid the shared source
of truth: one authored heightfield is referenced by a terrain, by a physics
collider, and by the navigation bake, none of which depend on the terrain
renderer to read heights. (Navigation already ships; sampling heights into a
navmesh is the deferred terrain<->nav integration - see "Explicitly
deferred" - and consumes the heightfield resource directly.)

- `Code/Foundation/Heightfield` - module `foundation.heightfield`, alias
  `Foundation::Heightfield`. The pure grid, NO RHI, NO terrain concepts:
  u16 height storage over a world XZ extent + Y range, bilinear GetHeightAt
  / GetNormalAt, world<->grid conversions, ray query, per-cell/chunk
  bounds (min/max height per cell, for culling + ray acceleration). Depends
  on nothing but Foundation::Core math.
- `Code/Foundation/Heightfield.Resource` - module
  `foundation.heightfield.resource`. The cooked, REFERENCEABLE heightfield
  resource + loader (CPU grid; `Ref<Heightfield>` points here). Terrain, the
  physics shape cook, and the nav bake all resolve this - no one embeds the
  raw grid.
- `Code/Pipeline/Heightfield.Pipeline` - HeightfieldAsset (Pipeline
  domain): created blank (size + world extent + Y range) OR from an imported
  16-bit heightmap image (PNG/RAW through the existing image import path).
  Builder cooks asset -> heightfield resource. The heightmap SOURCE stays
  binary (16-bit); the asset XML references the height blob the way meshes
  reference their data. Bump kBuilderCount + tripwire; kImporterCount for the
  heightmap-file drag-drop importer. (This is where heightmap import lives -
  NOT in Terrain.Pipeline.)
- `Code/Editor/Editor.Heightfield` - the heightfield asset page + New/Import
  wizard. P1 = create/import + a basic preview; the sculpt brushes (phase 2)
  operate on THIS asset through the shared brush framework. Derived-texture
  bakes (normal/occlusion FROM the heightfield) and erosion/hydraulic
  filters are Traktor features we DEFER (see "Explicitly deferred").
- `Code/Foundation/Terrain` - module `foundation.terrain`, alias
  `Foundation::Terrain`. DEPENDS ON `foundation.heightfield`; adds the
  terrain model over the grid: chunk quadtree + per-chunk LOD selection
  given a camera (pure function - unit-testable without rendering) and
  splat layer descriptors. Still NO RHI.
- `Code/Foundation/Terrain.Resource` - module
  `foundation.terrain.resource`. Cooked terrain resource + loader: a
  `Ref<Heightfield>` (the referenced heightfield resource, NOT an embedded
  blob), splatmap texture refs, per-layer material/texture refs (model-A GPU
  factory pattern like Texture.Resource).
- `Code/Pipeline/Terrain.Pipeline` - TerrainAsset (Pipeline domain):
  REFERENCES a heightfield asset (Ref) + a splat layer list referencing
  materials/textures + cast-shadows flag. No heightmap import here (that is
  Heightfield.Pipeline); the builder cooks asset -> product that carries the
  heightfield reference. Bump kBuilderCount + tripwire.
- `Code/Engine/Terrain` - module `engine.terrain`. TerrainComponent
  {Ref<TerrainAsset product>, cast shadows flag} + the RENDERER, owned
  HERE via the dynamic-category / per-item rendererId dispatch (sprites
  precedent) - Engine.Render stays terrain-free. Physics wiring: cooks a
  Jolt HeightFieldShape from the REFERENCED heightfield resource so render
  and collision cannot diverge (Physics depends on foundation.heightfield
  [+.resource], NOT foundation.terrain); registers through Engine.Physics'
  existing shape seam. Because physics references a heightfield resource
  directly, P1 ALSO ships a standalone heightfield COLLIDER: a
  `ShapeKind::Heightfield` option on the physics rigidbody/collider
  component carrying a `Ref<Heightfield>`, so a heightfield collision
  surface needs no terrain renderer at all (lands via Engine.Physics, same
  Jolt HeightFieldShape cook seam terrain uses).
  Component checklist: displayName + category attributes, InspectorView
  ref-picker entry, reflected GetHeightAt for scripts (natural types - no
  new facade lib; facade name count unchanged unless a subsystem facade
  proves necessary, then bump).
- `Code/Editor/Editor.Terrain` - phase 2: the brush editor + splat
  painting + terrain asset page. Phase 1 needs only the component picker
  + New Asset creator (no new editor lib until phase 2).

## Instance model (clarified 2026-08-10)

Terrain is a REGULAR COMPONENT, not a scene singleton: an entity with a
TerrainComponent, positioned by the entity transform; zero-to-many per
scene. Asset/instance split works like meshes (shared asset = shared
edits, the normal consequence). Multiple terrains = TILING, the pre-
streaming way to build bigger worlds; P1 has NO neighbor stitching -
matching tile edge heights is the author's job, and cross-entity LOD
seams are only hidden by skirts (proper stitching rides the deferred
large-world item). Transforms: translation + Y-rotation; the heightfield
is axis-aligned in LOCAL space and renderer/physics/raycast all apply the
entity transform uniformly (sculpt raycasts via inverse transform).
Height queries: component answers within its footprint; a subsystem
helper resolves "height under world point" across terrains - the same
seam navigation's bake will consume later.

## Rendering design (phase 1)

- Shared grid vertex buffer (one chunk-sized grid, e.g. 65x65 verts),
  per-chunk instance data {chunk origin, LOD, morph params}; vertex
  shader fetches height via textureLoad (unfiltered fetch works in VS on
  all three backends incl. WebGPU - verify at bring-up, it is the load-
  bearing portability assumption).
- Quadtree chunks, LOD by screen-space error/distance; SKIRTS to hide
  cracks in P1 (simplest correct), CDLOD-style morph as a P2 polish item.
- Normals from heightmap in the pixel shader; splat blend (one RGBA
  splatmap, up to 4 layers in P1) over the standard PBR lit path -
  terrain participates in clustered lights, CSM (casts + receives), IBL,
  fog, like any lit surface. New .hlsl files through the shaders track
  provider (hot reload for free); cooked WGSL via the existing pipeline.
- Per-chunk frustum culling through the existing view culling; chunk
  bounds from the height range of the chunk (computed at cook).
- Shadows: terrain casts into CSM by default (Traktor lesson: terrain
  cast shadow default-on).

## Physics (phase 1)

Jolt HeightFieldShape built from the REFERENCED heightfield resource at
scene integration (Physics.Pipeline cooks the shape data alongside the
heightfield product so runtime creation is cheap). Physics depends on
foundation.heightfield [+.resource] only - never on foundation.terrain.
Collision layer/group per the existing matrix. The height query epsilon test
below is the honesty check between render and physics (both resolve the SAME
heightfield resource, so it should hold trivially - the test guards against a
cook/quantization drift).

Standalone heightfield collider (P1): the physics rigidbody/collider
component gains `ShapeKind::Heightfield` + a `Ref<Heightfield>`, cooking the
same Jolt HeightFieldShape from a referenced heightfield resource WITHOUT a
TerrainComponent. This is why heightfield is its own asset - a collision
surface (a hill, a valley floor) can exist with no rendered terrain, and
terrain simply becomes "a heightfield collider that also renders". Lands in
Engine.Physics through the existing shape seam; bump the ShapeKind wire
version + the shape-cook count guard.

## Editor experience (phase 2 - separate discussion pending)

DECIDED (2026-08-10, editing-experience discussion): brushes are VIEWPORT
TOOLS in a shared tool-mode framework - IViewportTool interface + registry
in a THIN dedicated lib Editor/Editor.ViewportTools (NOT Editor.Core: the
tool contract speaks viewport input/pick rays/overlay draw, and Editor.Core
is deliberately UI-FREE - headless hosts link it; user catch 2026-08-10).
Editor.ViewportTools links the viewport/input layer only, no pages. The
palette/input-capture/mode state machine live in Editor.Scene,
tool DEFINITIONS registered explicitly from the owning domain editor libs
(Editor.Terrain registers sculpt/splat tools; explicit registrar + tripwire,
never discovery). Tools declare an availability predicate (terrain present)
and emit UNDO COMMANDS only (one per stroke); select+gizmos becomes the
default tool in the same framework so the viewport has ONE input-routing
path. The framework is a small prerequisite piece of this phase, specced
with it.

Brush framework in the scene viewport: raycast against the heightfield
(foundation.heightfield math, not physics), brush cursor decal, elevate /
lower / smooth / flatten (ctrl-picks target height) / splat paint. Undo =
region-delta commands (capture touched rect before/after, not whole-map
copies). Edits mark the ASSET dirty (asset-level save, scene stays
clean). Detailed UX to be specced after discussion - this section only
fixes the architectural seams: brushes are editor-side commands over
foundation.heightfield data (the height grid in P1; cut/attribute grids
ride the deferred holes item); the runtime never links brush code.

## Phase 3: grass

Per-layer grass rules (mesh, density, distance) rendered through the
instanced-mesh path (persistent per-set buffers - the MultiMesh work).
Spec'd in detail when phase 2 lands.

## Tests (required)

- foundation.heightfield: height/normal sampling (exact grid points,
  bilinear midpoints, edge clamp), world<->grid round-trip, ray query hits,
  per-cell bounds correctness.
- foundation.terrain: LOD selection determinism for fixture cameras, chunk
  quadtree correctness, splat descriptor round-trip.
- Heightfield cook round-trip: blank + imported-heightmap assets ->
  heightfield resource; resource loads into a grid matching the source.
- Terrain cook round-trip: TerrainAsset references a heightfield asset +
  splat layers; product loads and RESOLVES the heightfield reference; count
  guards on layers.
- Render smoke: pixel-probe test on the backend suite (a known ramp
  heightfield renders non-black, silhouette sanity) - Vulkan + WebGPU,
  per the VG.Backend.Tests precedent.
- Physics consistency: for N random points, Jolt heightfield hit height
  == foundation.heightfield GetHeightAt within epsilon (the render/collision
  divergence tripwire).
- Standalone heightfield collider: a rigidbody/collider component with
  ShapeKind::Heightfield + a Ref<Heightfield> and NO terrain builds a Jolt
  shape and a dropped body rests at GetHeightAt (proves the collider works
  without a TerrainComponent); ShapeKind wire round-trip.
- Component wire round-trip; tripwires (builder count x2, picker entry,
  shape-cook count).

## Acceptance (phase 1)

Battery green both compilers; wasm target renders the demo terrain; a
demo scene (imported heightmap, 4 splat layers, lit + shadowed, a physics
sphere rolling on it, an agent walking on it once navigation lands) runs
in play-in-editor and export; user visual pass on desktop + web.

## Review of the 2026-08-22/23 restructure (Fable, 2026-08-23)

Reviewed: the heightfield-as-first-class-asset split (e2b20534, a017fafd) and
the standalone-collider-into-P1 fold (931fda86). VERDICT: the restructure is
right and buildable as written; four gaps to close before/while building.

Confirmed against the code and conventions:

- The asset split matches the house `foundation.X` / `X.Resource` /
  `X.Pipeline` / `Editor.X` chain exactly, and the binary height blob follows
  the bulk-data sidecar rule (never inline in asset XML). The Traktor
  precedent audit was the right tiebreaker: physics and nav consuming heights
  WITHOUT the renderer is the property that pays forever.
- `ShapeKind::Heightfield` on the component is the correct placement -
  VERIFIED: ShapeKind lives on RigidBody/Collider today (PhysicsComponents:
  `ShapeKind shape` + per-kind fields; Cooked carries `Ref<CollisionShape>`,
  Plane carries planeHalfExtent). The new kind + `Ref<Heightfield>` lands
  exactly like Plane/Cooked did; the named wire-version bump is required
  (serializer strict versioning). A dedicated kind (not routing through
  Cooked) is right BECAUSE the heightfield is shared with rendering - a
  physics-only cooked blob would fork the source of truth.
- Folding the standalone collider into P1 is good sequencing: the heightfield
  chain gets a shippable consumer + real tests before the renderer exists,
  and "terrain = a heightfield collider that also renders" is the right
  dependency direction.

Gaps to close:

1. **The size contract is unstated, and the consumers disagree.** VERIFIED in
   vendored Jolt (HeightFieldShape.h): sample grids are SQUARE only (one
   mSampleCount), and `sampleCount / blockSize` must be >= 2 (power of 2 most
   efficient). Meanwhile 65-vert render chunks want `64k + 1` samples per
   side, and PNG import accepts arbitrary WxH. Decide ONCE at the asset
   layer: which sizes are authorable (recommend: square, `64k + 1`, k >= 1),
   and the physics cook pads/crops to Jolt's constraint with a VALIDATION
   ERROR at cook time for anything else - never a runtime surprise.
2. **GPU height-texture ownership.** Heightfield.Resource is deliberately a
   CPU grid (nav/physics consumers must not pay GPU) - so the VS textureLoad
   height texture must be created engine.terrain-side at component
   integration, uploaded from the CPU grid, and CACHED per heightfield
   resource so two terrains sharing one heightfield share one texture.
   Invalidate by resource version/generation (the bind-group-cache rule),
   which is also the phase-2 sculpt re-upload path. One sentence in the
   Rendering section fixes this; without it the texture tends to get built
   per-component.
3. **Ref-picker entries are plural.** THREE new `Ref<T>` component fields
   ship in P1: TerrainComponent's terrain ref, and `Ref<Heightfield>` on BOTH
   RigidBodyComponent and ColliderComponent. Each needs its InspectorView
   dispatch entry or the inspector shows no picker (the standing ref-picker
   rule); the Tests line's singular "picker entry" undercounts.
4. **P1 heightfield preview should be 2D.** When Editor.Heightfield lands,
   engine.terrain does not exist yet - there is nothing to 3D-render a
   heightfield with, and PreviewViewport hosts scenes. Say explicitly: P1
   preview = grayscale height image (+ min/max/extent readout); the 3D
   preview falls out free in phase 2 as a preview scene with a
   TerrainComponent.

## Explicitly deferred

Holes, CDLOD morphing, more than 4 splat layers / multiple splatmaps,
grass (phase 3), terrain as navigation bake source (joins the nav track
when both exist), ocean/river/forest (not planned), large-world paging.
Heightfield-editor extras Traktor has but we defer: erosion/hydraulic
filters and derived-texture bakes (normal/occlusion generated FROM the
heightfield). (The standalone heightfield-only physics collider is NOT
deferred - it ships in P1; see Physics.)
