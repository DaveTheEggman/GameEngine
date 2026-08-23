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

Heightfield is a SEPARATE library from terrain (2026-08-22 decision,
mirroring Traktor's `hf` factoring): the grid + sampling math is the shared
source of truth that both the terrain renderer AND physics consume, so it
lives below terrain and neither the physics shape cook nor the navigation
bake needs to depend on the terrain renderer to read heights. Navigation
already ships; sampling terrain heights into a navmesh is the deferred
terrain<->nav integration (see "Explicitly deferred"), and when it lands it
consumes foundation.heightfield directly.

- `Code/Foundation/Heightfield` - module `foundation.heightfield`, alias
  `Foundation::Heightfield`. The pure grid, NO RHI, NO terrain concepts:
  u16 height storage over a world XZ extent + Y range, bilinear GetHeightAt
  / GetNormalAt, world<->grid conversions, ray query, per-cell/chunk
  bounds (min/max height per cell, for culling + ray acceleration). This is
  what Physics cooks its collision shape from and what the editor brush
  raycasts against - it depends on nothing but Foundation::Core math. It
  OWNS its blob (de)serialization (self-describing, like the geometry mesh
  format), so any cook can write the grid without a heightfield pipeline lib.
- NO separate `Heightfield.Pipeline` / `Heightfield.Resource` in P1
  (2026-08-22): heightfield is a shared RUNTIME lib (many consumers) but
  has ONE authoring source - terrain - so `Terrain.Pipeline` imports the
  heightmap image and cooks the blob (using foundation.heightfield's
  serialize), and `foundation.terrain.resource` loads it back into a grid.
  A standalone pipeline would be speculative. Promote to `Heightfield.*`
  cook/resource libs ONLY when a NON-terrain authoring consumer appears
  (e.g. a standalone collision-heightfield asset, or a heightfield brush
  used outside terrain) - explicit-when-needed, not up front.
- `Code/Foundation/Terrain` - module `foundation.terrain`, alias
  `Foundation::Terrain`. DEPENDS ON `foundation.heightfield`; adds the
  terrain model over the grid: chunk quadtree + per-chunk LOD selection
  given a camera (pure function - unit-testable without rendering) and
  splat layer descriptors. Still NO RHI.
- `Code/Foundation/Terrain.Resource` - module
  `foundation.terrain.resource`. Cooked terrain resource + loader:
  heightfield blob (loads into a `foundation.heightfield` grid), splatmap
  texture refs, per-layer material/texture refs (model-A GPU factory
  pattern like Texture.Resource).
- `Code/Pipeline/Terrain.Pipeline` - TerrainAsset (Pipeline domain):
  created blank (size + scales) or from an imported heightmap image
  (16-bit PNG/RAW through the existing image import path); splat layer
  list referencing materials/textures. Builder cooks asset -> product.
  Heightmap SOURCE data stays binary (16-bit) - scenes stay text; the
  asset XML references the height blob the way meshes reference their
  data. Bump kBuilderCount + tripwire; importer only if heightmap-file
  drag-drop is wanted (then bump kImporterCount too).
- `Code/Engine/Terrain` - module `engine.terrain`. TerrainComponent
  {Ref<TerrainAsset product>, cast shadows flag} + the RENDERER, owned
  HERE via the dynamic-category / per-item rendererId dispatch (sprites
  precedent) - Engine.Render stays terrain-free. Physics wiring: cooks a
  Jolt HeightFieldShape from the same `foundation.heightfield` grid so
  render and collision cannot diverge (Physics depends on
  foundation.heightfield, NOT foundation.terrain); registers through
  Engine.Physics' existing shape seam.
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

Jolt HeightFieldShape built from the `foundation.heightfield` grid at scene
integration (Physics.Pipeline cooks the shape data alongside the terrain
product so runtime creation is cheap). Physics depends on
foundation.heightfield only - never on foundation.terrain. Collision
layer/group per the existing matrix. The height query epsilon test below is
the honesty check between render and physics (both sample the SAME grid, so
it should hold trivially - the test guards against a cook/quantization
drift).

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
- Cook round-trip: blank + imported-heightmap assets; product loads;
  count guards on layers.
- Render smoke: pixel-probe test on the backend suite (a known ramp
  heightfield renders non-black, silhouette sanity) - Vulkan + WebGPU,
  per the VG.Backend.Tests precedent.
- Physics consistency: for N random points, Jolt heightfield hit height
  == foundation.terrain GetHeightAt within epsilon (the render/collision
  divergence tripwire).
- Component wire round-trip; tripwires (builder count, picker entry).

## Acceptance (phase 1)

Battery green both compilers; wasm target renders the demo terrain; a
demo scene (imported heightmap, 4 splat layers, lit + shadowed, a physics
sphere rolling on it, an agent walking on it once navigation lands) runs
in play-in-editor and export; user visual pass on desktop + web.

## Explicitly deferred

Holes, CDLOD morphing, more than 4 splat layers / multiple splatmaps,
grass (phase 3), terrain as navigation bake source (joins the nav track
when both exist), ocean/river/forest (not planned), large-world paging.
