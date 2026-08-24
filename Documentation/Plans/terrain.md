# Terrain

**Status:** BUILDING P1 (started 2026-08-23). The HEIGHTFIELD half is complete
and shipped (see "Build progress"); the TERRAIN-rendering half is next. Third
of the three parity P0 tracks (after property-animation.md and navigation.md -
see docs/design/parity-2026-08.md). References: Lumix terrain (heightmap +
splat + grass) and Traktor's richer brush architecture
(elevate/flatten/smooth/cut/color/attribute brushes + align-to-terrain
operators) for the phase-2 editor.

## Build progress (2026-08-23)

DONE (green on clang + gcc, each with cook round-trip / integration tests):
- `foundation.heightfield` - the Object grid + sampling (bilinear height/normal,
  world<->grid, ray-march, cell bounds); size contract 64k+1.
- `foundation.heightfield.resource` - cooked resource: metadata object + a
  "heights" SIDECAR stream + factory (Ref<Heightfield>).
- `heightfield.pipeline` - HeightfieldAsset + builder + importer (blank +
  16-bit heightmap import & bilinear resample).
- `foundation.image.io` - added PixelFormat::R16 + LoadImage16 (true 16-bit
  height precision; the general LoadImage down-samples to 8-bit).
- STANDALONE HEIGHTFIELD COLLIDER - the spec's early consumer, proving the
  chain end-to-end with NO renderer: ShapeKind::Heightfield (Jolt
  HeightFieldShape, self-padded per the correction) in foundation.physics;
  Ref<Heightfield> on RigidBody/Collider + the u16->f32 integration + DataVersion
  1->2 (v2 wire); InspectorView ref-picker dispatch.
- `Editor.Heightfield` - the asset page: permanent 2D grayscale height preview +
  readout + the authored fields (undo + re-cook); registered in
  Pipeline.Registration (builder/importer/type; counts 23->24, 7->8) and
  Tools.Editor.

- `foundation.terrain` - the terrain model over a heightfield: BuildChunks (k x k
  chunks with LOCAL-space AABBs), per-chunk LOD selection via the SHARED
  coverage metric (SelectChunkLod delegates to foundation.lod's
  ProjectedSphereCoverage + SelectLevelByCoverage - one formula with meshes, now
  unit-testable headless), a TerrainQuadtree for hierarchical frustum culling
  (via the existing BoundingFrustum), the SplatLayer descriptor, AND the shared
  chunk grid mesh (BuildChunkGridVertices 65x65 + BuildChunkGridIndices(lod), the
  geo-mipmapping geometry the renderer uploads once). No RHI; 7 tests.
- `foundation.terrain.resource` - the cooked terrain resource: TerrainSource
  (heightfield + splatmap + per-layer albedo guids + tiling + castShadows, the
  Material.Resource pattern) -> TerrainResource product (resolved
  Proxy<Heightfield> + Proxy<Texture> splatmap + layers) via TerrainFactory.
  (The product was renamed Terrain -> TerrainResource for the Resource-suffix
  convention.) The heightfield is the SHARED source of truth (physics/nav
  resolve the same id). 2 tests incl. a content-DB build resolving the shared
  heightfield.
- `Terrain.Pipeline` - TerrainAsset (references a heightfield asset + splatmap +
  per-layer albedo + tiling + castShadows) + a reference-pass-through builder ->
  the TerrainResource; registered in Pipeline.Registration (builder count 25).
  1 cook-through test.
- `engine.terrain` PHASE A (the authoring surface; RHI-free): TerrainComponent
  {Ref<TerrainResource>, castShadows, visible} + manager + reflection,
  registered in Engine.SceneSurface (the SceneModule pair; ModuleCount 9 -> 10)
  + the InspectorView Ref<TerrainResource> picker (gap 3). 2 tests (reflection +
  scene serialize round-trip).
- `engine.terrain` PHASE B (first RHI primitive): TerrainHeightTextureCache
  (:heighttexture partition) - each heightfield's u16 grid uploaded to an
  R16Uint 2D texture (the VS will textureLoad it), CACHED by resource id+version
  (shared across terrains; version bump = sculpt re-upload). Test on the Null
  device (hit / rebuild-in-place / second-id / empty / clear).

REMAINING (the terrain-rendering half): `engine.terrain` PHASES B+ - the
chunked geo-mipmap RENDERER (the RHI/shader draw path; all the RHI-FREE inputs
are now built + tested - chunks, LOD via foundation.lod, quadtree cull, the
shared grid mesh geometry). The sub-phases:
- B GPU height texture: DONE - TerrainHeightTextureCache (R16Uint, cached by
  resource id+version).
- C the Renderer: DONE. TerrainRenderData (one whole-terrain item), the
  TerrainComponentManager as the scene's IRenderDataProvider (builds the chunk
  model per heightfield + gets the GPU height texture, emits the item), a
  TerrainRenderer riding Opaque via RegisterRenderer, and the TerrainSubsystem
  wiring both into RenderSubsystem (particles precedent). The renderer uploads the
  shared 65x65 grid VB + one index buffer PER LOD once, and its Resolve replays
  ExtractVisibleChunkDraws per view (quadtree cull + shared-coverage LOD, the same
  tested CPU path) folding chunkToWorld into a per-terrain ViewProj, then emits one
  DrawIndexed per visible chunk at its LOD. Bind sets: 0 view, 1 per-chunk, 2
  height texture. Headless test drives the real RenderFrame over the Null RHI +
  DXC (4 chunks drawn looking down, 0 off-screen) - green on clang + gcc. Module
  restructured: engine.terrain is now an aggregator over :heighttexture /
  :renderdata / :components / :renderer / :subsystem partitions.
- D shaders: DONE (D1, height-lit). terrain.vs.hlsl fetches height via an integer
  Load on the R16Uint texture (the load-bearing WebGPU portability bet) + derives
  the normal by central differences; terrain.ps.hlsl does directional + ambient
  over a height/slope colour ramp. Authored as HLSL through the file shader
  provider (pipeline cooks SPIR-V/WGSL); compiled by the Phase-C headless test.
  REMAINING D2 (splat): blend the RGBA splatmap over the per-layer albedo textures
  (TerrainResource::layers) as a 4th material set - needs the splat/layer assets
  cooked + bound.
- HOST WIRING: DONE. DefaultApplication registers the TerrainSubsystem in the
  graphics-guarded block next to ParticleSubsystem, so a TerrainComponent renders
  in any running scene (the manager is already injected by scene composition).
  Full app links + runs green on clang + gcc.
- E polish: PARTLY DONE.
  - Offscreen pixel-probe render test (Vulkan): DONE. Engine.Terrain.Backend.Tests
    renders a lit dome top-down on a real Vulkan device + reads pixels back; asserts
    coverage (98% filled) + directional-shading asymmetry, and (2nd case) that
    flipping the scene sun inverts the asymmetry. The render ground truth.
  - Back-face cull: DONE (grid winds CCW-from-above; probe confirms). Note: a
    heightfield is a single surface so this is nearly a no-op for the surface - it
    earns its keep once skirts add side-facing walls.
  - Real directional-sun wiring: DONE (Resolve reads the scene's first directional
    light; fixed key light is the fallback).
  - Skirts (LOD-seam crack-hiding): DONE. The shared grid VB carries a surface +
    a skirt copy; per-LOD skirt walls drop below the surface (VS) around the 4 chunk
    edges. A SetSkirtsEnabled toggle + the exact surface/skirt index split are
    tested; pixel-level crack repro was too view-dependent for a stable test, so
    correctness rests on the structural test + no-regression.
  - GBuffer output: DONE. Terrain is always opaque, so it writes the full forward
    GBuffer (colour + octahedral view-normal + motion vector + material), not just
    colour - required by the pass's 4-target MRT (WebGPU rejects a mismatch; Vulkan
    tolerated it but fed garbage to SSR/TAA/motion). The WebGPU probe caught this.
  - WebGPU probe: DONE. The pixel probe runs on Vulkan AND WebGPU and requires a
    match - filled + total are identical on both, so the WGSL cook (the integer Load
    on the R16Uint height texture, the portability bet) is pixel-exact vs SPIR-V.
  - CSM shadows: DONE. Terrain CASTS (TerrainRenderer::ResolveDepthOnly - the camera
    prepass + each cascade - via a vertex-only terrain_depth PSO, surface indices
    only) and RECEIVES (the PS ports forward's SampleCSM: PCF + normal-offset bias +
    cascade blend + far fade, attenuating the sun term). The VS unfolds to world
    space (worldPos + world normal) so the cascade sampling is world-space. Verified
    by a Vulkan shadow probe: a ridge casts onto flat ground, one band -> 41% with
    shadows on, 0.000 asym off. (Prereq fix: BuildShadowCasterList now reads generic
    base fields, not a blind MeshRenderData downcast.)
  - REMAINING: splat (D2 above).
Then `Editor.Terrain` (phase 2).

KNOWN GAP for review (2026-08-23, Opus): the MCP/agent import tool
(Editor.Mcp/ProjectTools) resolves an extension with the SINGULAR
ImporterRegistry::FindFor, which returns the FIRST registered claimant. `.png`
is claimed by Texture (first), Image, then Heightfield, so an AGENT importing a
.png always gets Texture and cannot select Image or Heightfield. This is
PRE-EXISTING (Image already had it) and NOT specific to terrain, but the
heightfield importer makes it a three-way collision. The interactive editor is
unaffected (drag-drop/Import... uses FindAllFor + an importer chooser menu, and
Heightfield is selectable; `.r16` is heightfield-only so it needs no chooser).
Fix option if wanted: an optional `importer` hint on the MCP import tool to
disambiguate. Flagged here for a Fable ruling; no code change made.

RULING (Fable, 2026-08-23): build the hint, plus discoverability. The MCP
import tool resolves with FindAllFor (the interactive path's registry call):
an optional `importer` parameter selects among claimants by name; WITHOUT the
hint an ambiguous extension keeps today's first-claimant default for
compatibility BUT the tool result names the alternatives (so an agent can see
"imported as Texture; also claimable by Image, Heightfield" and re-import with
the hint). Unknown hint = error listing valid claimants. Small, agent-parity
with the human chooser, no registry change. Lands whenever MCP work is next
touched - not a terrain-track blocker.

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
  SIZE CONTRACT (gap 1, 2026-08-23): a heightfield grid is SQUARE with side
  `S = 64*k + 1` (k >= 1: 65, 129, 257, 513, 1025, ...). This one rule
  satisfies both consumers: render chunks are 64-quad (65-vert) tiles, so S
  tiles into k*k chunks sharing edges; and Jolt's HeightFieldShape is square
  (one sampleCount). The type exposes the invariant (a validating factory /
  IsValidSize helper); non-conforming sizes never reach the runtime.
- `Code/Foundation/Heightfield.Resource` - module
  `foundation.heightfield.resource`. The cooked, REFERENCEABLE heightfield
  resource + loader (CPU grid; `Ref<Heightfield>` points here). Terrain, the
  physics shape build, and the nav bake all resolve this - no one embeds the
  raw grid.
- `Code/Pipeline/Heightfield.Pipeline` - HeightfieldAsset (Pipeline
  domain): created blank (a valid square size + world extent + Y range) OR
  from an imported 16-bit heightmap image (PNG/RAW through the existing image
  import path), RESAMPLED to a chosen valid square size `S = 64*k + 1` (the
  import wizard picks the target resolution; blank is valid by construction).
  Builder cooks asset -> heightfield resource. The heightmap SOURCE stays
  binary (16-bit); the asset XML references the height blob the way meshes
  reference their data (bulk-data sidecar rule - never inline). Bump
  kBuilderCount + tripwire; kImporterCount for the heightmap-file drag-drop
  importer. (This is where heightmap import lives - NOT in Terrain.Pipeline.)
  The physics HeightFieldShape is BUILT at scene integration (Engine.Physics,
  not a pipeline cook) from this grid; see Physics for the Jolt block-size
  padding + validity guard.
- `Code/Editor/Editor.Heightfield` - the heightfield asset page + New/Import
  wizard. Preview is 2D BY ASSET IDENTITY, permanently (user ruling
  2026-08-23, sharpening gap 4): a heightfield IS an image of heights, so a
  grayscale height view + min/max/extent readout is its faithful, complete
  preview - the texture/image page precedent, NOT a placeholder awaiting 3D.
  The 3D preview belongs to the TERRAIN asset (heights + splats + materials,
  lit): phase 2's Editor.Terrain page previews it via a TerrainComponent
  preview scene. Each page previews what its asset actually is; no interim
  wireframe needed. The sculpt brushes (phase 2)
  operate on THIS asset through the shared brush framework (in the SCENE
  viewport, where terrain context exists - not on this page). Derived-texture
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
  precedent) - Engine.Render stays terrain-free. Physics wiring: builds a
  Jolt HeightFieldShape at integration from the REFERENCED heightfield
  resource so render and collision cannot diverge (Physics depends on
  foundation.heightfield
  [+.resource], NOT foundation.terrain); registers through Engine.Physics'
  existing shape seam. Because physics references a heightfield resource
  directly, P1 ALSO ships a standalone heightfield COLLIDER: a
  `ShapeKind::Heightfield` option on the physics rigidbody/collider
  component carrying a `Ref<Heightfield>`, so a heightfield collision
  surface needs no terrain renderer at all (lands via Engine.Physics, same
  Jolt HeightFieldShape build seam terrain uses).
  Component checklist: displayName + category attributes, reflected
  GetHeightAt for scripts (natural types - no new facade lib; facade name
  count unchanged unless a subsystem facade proves necessary, then bump),
  and InspectorView ref-picker DISPATCH for BOTH new ref types (gap 3,
  2026-08-23): `Ref<TerrainAsset>` (TerrainComponent) and `Ref<Heightfield>`
  (on RigidBodyComponent AND ColliderComponent) - three new Ref<T> fields
  across two dispatch entries; without them the inspector shows no picker
  (standing ref-picker rule).
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

- CHUNK LOD METRIC (Fable note, 2026-08-23 - adopt before the renderer
  half hardens): mesh-lod P1 built and exported the pure selection math
  in foundation.render - `LodCoverageFor` (projected screen coverage
  from bounds + camera: fov-aware, resolution-aware, ortho-correct,
  unit-tested) plus the +-5% hysteresis band pattern. foundation.terrain's
  shipped `DistanceToChunk + SelectLod` uses raw DISTANCE, which cannot
  see fov or viewport size (a chunk at 100m fills very different screen
  area at 30 vs 90 degrees). EVALUATE switching the chunk metric to
  coverage (wrap or reuse LodCoverageFor on the chunk's bounding sphere)
  while the selection is still a pure function with tests - this is the
  "check-at-P1 whether the formula is genuinely shareable" item from the
  mesh-lod refresh, answered: it exists, it fits, share it rather than
  invent formula #2. Chunk-boundary popping wants the same hysteresis
  band; ApplyLodHysteresis is mesh-coupled today but generalizes to a
  Span<f32> threshold walk trivially if literal sharing is wanted.
- LAYERING ISSUE for Fable to resolve (2026-08-23, Opus): sharing
  LodCoverageFor collides with the module layering. LodCoverageFor lives in
  `foundation.render` (MeshRenderer.cppm), which imports `foundation.rhi` -
  and `foundation.terrain` is a NO-RHI foundation module (Core + Heightfield
  only), so it CANNOT call LodCoverageFor to compute coverage. LodCoverageFor
  also takes a `ViewCamera` (a render type). PickLodLevel/ApplyLodHysteresis
  are additionally mesh-coupled. So the shared coverage FORMULA sits below an
  RHI ceiling that foundation.terrain sits above.
  INTERIM (built, keeps foundation.terrain RHI-free + the selection pure and
  tested): foundation.terrain adds `ChunkBoundingSphere` (the bridge input)
  and `SelectLodByCoverage(coverage, Span<const f32> thresholds)` (the pure
  descending-threshold walk, mesh-lod's PickLodLevel shape generalized to a
  Span). engine.terrain (which DOES depend on foundation.render) computes the
  per-chunk coverage with LodCoverageFor on the world sphere and feeds it to
  SelectLodByCoverage. The distance metric (SelectLod/DistanceToChunk) stays
  as the RHI-free fallback. This shares the formula (no formula #2) but the
  coverage COMPUTATION is only exercised in engine.terrain, not unit-tested in
  foundation.terrain.
  RULING (Fable, 2026-08-23; REVISED same day on user review): the shared
  home is `Foundation::Lod` (`foundation.lod`, Code/Foundation/Lod) - a
  dedicated Core-only leaf. First cut placed the functions in core's
  :bounds partition; the user ruled LOD is a DOMAIN concept core math
  must not know about, and the dedicated lib is also the designated home
  for future LOD-flavored math that would otherwise accrete into core.
  Relocated immediately (the cheapest moment - before terrain took the
  dependency): `foundation::lod::ProjectedSphereCoverage(view,
  projection, center, radius, bias)` + `SelectLevelByCoverage(Span<const
  f32>, coverage)` + `ApplyCoverageHysteresis(Span<const f32>, coverage,
  raw, last, band)`, with Lod.Tests pinning the whole surface.
  foundation.render's mesh-facing trio (LodCoverageFor/PickLodLevel/
  ApplyLodHysteresis) are thin adapters over Foundation::Lod - every
  mesh-lod call site and test unchanged. CONSEQUENCE FOR TERRAIN:
  foundation.terrain links Foundation::Lod (Core-only, RHI-free -
  layering clean) and calls it DIRECTLY: coverage computation AND the
  walk fully unit-testable at the foundation layer, no asymmetry, no
  formula #2. Opus: link Foundation::Lod, delegate SelectLodByCoverage
  to SelectLevelByCoverage (or use it directly), compute chunk coverage
  with ProjectedSphereCoverage on the chunk's bounding sphere; the
  distance metric may stay as a secondary heuristic or retire - your
  call in the module.
  DONE (Opus, 2026-08-23): foundation.terrain links Foundation::Lod and
  delegates - `SelectChunkLod(chunk, chunkToWorld, view, projection,
  thresholds, bias)` transforms the chunk's ChunkBoundingSphere to world,
  calls ProjectedSphereCoverage, then SelectLevelByCoverage; a
  SelectChunkLods batch wraps it. RETIRED the distance metric
  (DistanceToChunk/SelectLod) - one metric now. Coverage selection is
  unit-tested at the foundation layer (view/projection matrices in, LOD out).
- Shared grid vertex buffer (one chunk-sized grid, e.g. 65x65 verts),
  per-chunk instance data {chunk origin, LOD, morph params}; vertex
  shader fetches height via textureLoad (unfiltered fetch works in VS on
  all three backends incl. WebGPU - verify at bring-up, it is the load-
  bearing portability assumption).
- GPU height-texture ownership (gap 2, 2026-08-23): Heightfield.Resource is
  deliberately a CPU grid (nav/physics must not pay GPU), so the VS height
  texture is created ENGINE.TERRAIN-side at component integration, uploaded
  from the CPU grid, and CACHED keyed by the heightfield resource id +
  version - two terrains referencing one heightfield share one texture.
  Invalidate by resource version/generation (the bind-group-cache rule),
  which doubles as the phase-2 sculpt re-upload path.
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

Jolt HeightFieldShape built at scene integration DIRECTLY from the
referenced heightfield resource's CPU grid (like Plane builds from its
params - no pre-cooked shape blob: one product, no doubled height storage,
and the sculpt-rebuild path falls out; a derived pre-cook may RETURN later
as a load-time optimization without changing this model, since a blob
derived in the same cook cannot diverge).
NO BUILDER PADDING (corrected 2026-08-23 against vendored Jolt source):
Jolt's actual constraints are blockSize in [2,8], `sampleCount / blockSize
>= 2`, and an upper bound from sub-shape ID bits - NOT "a multiple of the
block size". The constructor itself rounds sampleCount up to a block
multiple and fills the padded rows/cols with cNoCollisionValue (no
collision), so the footprint stays exactly the authored extent. Our
builder passes the square `64k+1` grid through UNTOUCHED; do NOT
hand-pad by duplicating the trailing row/col - real heights in the
padding would widen the collision surface past the rendered terrain by up
to blockSize-1 cells. The square/valid check stays a cook-time guard that
errors rather than reshapes. MEASURE at bring-up: shape construction cost
at 1025x1025 (quantize + hierarchy build) at scene integration - that
number decides whether the pre-cook optimization is ever worth it.
Physics depends on foundation.heightfield
[+.resource] only - never on foundation.terrain. Collision layer/group per
the existing matrix. The height query epsilon test below is the honesty
check between render and physics (both resolve the SAME heightfield
resource, so it should hold trivially on interior samples - the test guards
against quantization drift; also assert a ray just OUTSIDE the authored
extent misses, pinning Jolt's no-collision padding).

Standalone heightfield collider (P1): the physics rigidbody/collider
component gains `ShapeKind::Heightfield` + a `Ref<Heightfield>`, cooking the
same Jolt HeightFieldShape from a referenced heightfield resource WITHOUT a
TerrainComponent. This is why heightfield is its own asset - a collision
surface (a hill, a valley floor) can exist with no rendered terrain, and
terrain simply becomes "a heightfield collider that also renders". Lands in
Engine.Physics through the existing shape seam; bump the ShapeKind wire
version + the shape-cook count guard.

## Editor experience (phase 2) - DIRECTION SET (Fable, 2026-08-23; user question answered)

THE ANSWER TO "dedicated terrain asset page OR edit-in-scene?": BOTH,
split by ROLE - and the split falls out of rulings already recorded:

**Editing happens IN THE SCENE VIEWPORT** (the property-animation
precedent, and what Editor.ViewportTools was built in anticipation of).
Editor.Terrain registers TWO tools into the existing IViewportTool
registry (explicit registrar + tripwire, the framework decision):
- **Sculpt** - elevate / lower / smooth / flatten (ctrl picks the target
  height), operating on the HEIGHTFIELD ASSET the scene's terrain
  references. Ray pick = foundation.heightfield::QueryRay through the
  inverse entity transform (exists); brush cursor = the viewport's KEYED
  debug list (editor chrome - never in camera preview); each stroke step
  rewrites samples + BumpVersion, riding the live re-upload path the
  playground already proves (height texture + chunk bounds rebuild,
  retire-queue safe). Undo = REGION-DELTA commands (touched-rect
  before/after, one entry per stroke - the 2026-08-10 decision). Edits
  mark the heightfield ASSET dirty (asset-level save; the scene stays
  clean); save routes through the normal asset save -> watcher recook.
- **Splat Paint** - layer selector + weight brush writing the SPLATMAP.
  Direction: the splatmap becomes an editor-writable IMAGE-backed asset
  (RGBA8) so painting has a source to save; texel writes re-upload live
  through the texture path, save persists the image source. Same
  region-delta undo shape. (Detailed data flow lands in the phase-2
  spec; this fixes only the seam: paint is a viewport tool over an
  image asset, not a bespoke canvas.)
Both tools declare the availability predicate (a TerrainComponent whose
terrain resolves is present), and their panels dock below the viewport
exactly like the property-animation panel (the persistent-panel
precedent; the parked tool_panel seam stays parked). Sculpting a
heightfield SHARED by several terrains edits all of them - the standard
shared-asset consequence, already recorded in the instance model.

**The dedicated TerrainPage is the COMPOSITION + PREVIEW surface, and
never hosts brushes.** Per the preview-identity ruling (heightfield page
= 2D permanently; the 3D preview belongs to the TERRAIN asset): a
bespoke page on the PreviewViewport substrate (mesh/material precedent)
with a TerrainComponent preview scene - orbit camera, the LOD/wireframe
toggles the substrate already offers - plus the asset's authoring
fields: heightfield + splatmap refs (pickers), the layer list (albedo
ref + tile scale per layer - the generic reflected list editor
renders it), castShadows default, and stats (grid size, chunk count,
memory). Keeping brushes OUT of the page preserves ONE input-routing
path for editing (the scene viewport's tool palette) instead of a
second brush host inside a preview - the same reasoning that put
select+gizmos and property animation in the one framework.

Sequencing inside phase 2: the TerrainPage first (it unblocks authored
cooked terrains for D2's splat path without any brush work), then
Sculpt, then Splat Paint (which wants the page's layer list to already
exist for its layer selector).

## Editor experience (phase 2 - original decision record)

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

GRASS SETS PARTITION PER CHUNK (Fable note, 2026-08-23 - a design
constraint to carry into the P3 spec): mesh-lod's per-SET instanced LOD
selection (shipped) picks one level from a set's MERGED bounds, which
degenerates on a terrain-wide grass set (merged bounds = the whole
terrain). Partitioning grass into one instanced set PER TERRAIN CHUNK -
the natural shape for chunked terrain anyway - makes each set's bounds
chunk-sized, and the already-shipped per-set selection becomes
meaningful FOR FREE: distant chunks' tufts draw their coarse level
(auto-generated chains work on grass meshes), near chunks draw fine,
no per-instance bucketing needed. Grass density/fade distance rules
stay terrain's own; this note is only about which instanced-set
granularity makes the existing LOD machinery work.

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
- Component wire round-trip; tripwires (builder count x2, shape-cook count,
  ref-picker dispatch for Ref<TerrainAsset> + Ref<Heightfield>).

## Acceptance (phase 1)

Battery green both compilers; wasm target renders the demo terrain; a
demo scene (imported heightmap, 4 splat layers, lit + shadowed, a physics
sphere rolling on it) runs in play-in-editor and export; user visual pass
on desktop + web. (An agent walking the terrain is NOT P1 acceptance:
navigation SHIPPED 2026-08-18, but terrain as a bake source is the
explicitly deferred terrain<->nav integration - the heightfield
tessellated into the existing triangle-soup NavigationMeshBuilder. The
agent demo is that integration's acceptance, not this one's. The stale
"once navigation lands" phrasing predated navigation's completion.)

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

Gaps to close (ALL CLOSED 2026-08-23, folded into the body):

- Gap 1 -> foundation.heightfield SIZE CONTRACT bullet + Heightfield.Pipeline
  physics-cook note (square `64k+1`; cook pads to Jolt block size, errors
  otherwise).
- Gap 2 -> Rendering "GPU height-texture ownership" bullet (engine.terrain
  owns it, cached per heightfield resource id+version).
- Gap 3 -> engine.terrain component checklist + Tests (ref-picker dispatch
  for Ref<TerrainAsset> and Ref<Heightfield>, three fields).
- Gap 4 -> Editor.Heightfield bullet (P1 preview = 2D grayscale + readout;
  3D preview is a phase-2 freebie).

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

### Second pass: review of the gap fold (Fable, 2026-08-23, commit 250f5fd1)

The fold is ACCEPTED with two corrections, both applied to the Physics
section above:

- **The padding instruction was wrong and is REVERSED.** The fold said the
  builder pads sampleCount to a block-size multiple by duplicating the
  trailing row/col. Verified against vendored Jolt source
  (HeightFieldShape.cpp constructor): Jolt rounds sampleCount up ITSELF and
  fills padding with cNoCollisionValue, keeping the footprint exact - and
  the stated constraint ("multiple of block size") misreads Jolt's actual
  requirement (`sampleCount / blockSize >= 2`). Hand-padding with real
  heights would have widened collision past the rendered terrain by up to
  blockSize-1 cells of phantom apron. Builder passes the grid through
  untouched; a new test line pins the just-outside-extent miss.
- **The runtime-build decision STANDS, its rationale is restated.** Dropping
  the Physics.Pipeline pre-cook is right, but not because a pre-cooked blob
  "forks the source of truth" (a blob derived in the same cook from the same
  product cannot diverge - the review's fork warning was about routing
  through ShapeKind::Cooked/CollisionShape assets, a different thing). The
  real reasons: one product, no doubled height storage, the Plane precedent,
  and the sculpt-rebuild path. Consequence worth keeping visible: the
  pre-cook may legitimately return later as a load-time optimization -
  measure 1025x1025 shape construction at bring-up (noted in Physics).

The resample-at-import addition (the wizard picks a valid `64k+1` target
resolution rather than rejecting nonconforming images) was not in the
review and is a good call - imports should not fail on the commonest
real-world case (a 1024x1024 heightmap).

## Explicitly deferred

Holes, CDLOD morphing, more than 4 splat layers / multiple splatmaps,
grass (phase 3), terrain as navigation bake source (navigation SHIPPED
2026-08-18; this is the deferred terrain<->nav integration - the heightfield
tessellated into the existing triangle-soup NavigationMeshBuilder - and its
own agent-walks-terrain acceptance, not P1's), ocean/river/forest (not
planned), large-world paging.
Heightfield-editor extras Traktor has but we defer: erosion/hydraulic
filters and derived-texture bakes (normal/occlusion generated FROM the
heightfield). (The standalone heightfield-only physics collider is NOT
deferred - it ships in P1; see Physics.)
