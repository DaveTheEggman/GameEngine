# Mesh LOD (levels of detail)

> STATUS: SPEC PREPARED 2026-08-15 (user-requested); implementation NOT
> scheduled this week. meshoptimizer dependency USER-APPROVED 2026-08-15.
> Read alongside Documentation/Specs/asset-variants.md (same week's sibling;
> LOD quality tiers could ride the variant axis later, but v1 deliberately
> does not - LODs are runtime-selected, not per-platform).

## Goal

Distance/coverage-appropriate geometry so dense scenes (Sponza-class and
beyond) spend triangles where they read. Two sources of truth, one wire
format: artist-authored LOD meshes when provided, auto-generated chains
when not (marketplace/imported content rarely ships LODs).

## Decision 1 - vendor meshoptimizer (approved)

`ThirdParty/meshoptimizer` (zeux) - industry standard, dependency-free,
MSVC-native. It provides BOTH:

- `meshopt_simplify` (+ `simplifySloppy` fallback) for auto-LOD chains;
- vertex-cache / overdraw / vertex-fetch optimization for EVERY cooked
  mesh - a free win independent of LOD, shipped as its own phase (P0)
  so the dependency pays for itself before LOD lands.

Heavy headers stay in Pipeline implementation units (GCC module hygiene).

## Decision 2 - data model: LODs live INSIDE the cooked mesh product

One mesh resource carries its whole LOD chain; LODs are NOT separate
assets (no reference fan-out, no picker churn, prefabs/components keep a
single `Ref<StaticMesh>`).

- The mesh-geometry binary sidecar (v3, `kMeshGeometryStreamName`) gains a
  per-LOD index-range table per submesh: vertices are SHARED across LODs
  (simplification only drops indices); each LOD level stores its own index
  ranges. Sidecar/asset version bump in the same commit (the bump IS the
  migration; existing meshes load as 1-LOD chains).
- Per-LOD data: index ranges per submesh + a precomputed SWITCH METRIC
  (normalized screen-coverage threshold, from simplification error or
  authored override).
- v1 scope: STATIC meshes only. Skinned LODs are deferred - simplification
  must respect skin weights/influences and remap the parallel skin stream;
  priced as its own phase (P4) after the static path proves the wire.

## Decision 3 - authoring: named LODs win, auto-generation fills

- **Authored:** the model importer recognizes the `_LOD1`/`_LOD2`/...
  node-name convention (ufbx surfaces names; FBX LODGroup nodes map to the
  same thing where present). `Foo`, `Foo_LOD1`, `Foo_LOD2` collapse into
  ONE mesh asset with a 3-level chain. Import dialog shows what it found.
- **Auto:** import dialog gains "Generate LODs" (default ON for models
  above a triangle budget, e.g. 10k): target ratios {1.0, 0.5, 0.25,
  0.125} with meshopt error bounds; levels that fail the error bound are
  dropped (a chain is as long as quality allows, never padded).
- Authored + auto never mix within one mesh: authored presence disables
  generation for that mesh.
- All knobs are AUTHORED data on the mesh asset (never runtime structs).

## Decision 4 - runtime selection: screen coverage, per view, with
## hysteresis

- Selection happens at EXTRACTION (render side already carries
  `worldCenter`/`worldRadius` per MeshRenderData): projected-sphere
  coverage against the view -> pick the finest LOD whose threshold passes.
  Per-VIEW selection (split-screen/editor-vs-game views disagree freely,
  matching per-view shadows/MSAA precedent).
- Hysteresis band (~10%) on the switch metric prevents boundary popping;
  no cross-fade/dithered transitions in v1 (noted follow-up).
- Component knobs on `MeshComponent`: `lodBias` (float, inspector-visible)
  and `forceLod` (int, -1 = auto; debug + cinematic use). Reflection
  attributes per the inspector rules (displayName/category).
- Shadows: casters use the SELECTED view LOD for cascades (camera-coupled)
  and the coarsest passing LOD for the camera-independent local-shadow
  caster list - shadows never select finer than the main view.
- `InstancedMeshComponent` (crowd sets): v1 selects ONE LOD per set from
  the set's aggregate bounds (the per-set persistent buffer stays intact).
  Per-instance LOD (bucketed draws) is the deferred v2 - noted, priced
  separately, do not build in v1.
- Culling stays untouched (LOD selection composes after the existing
  sphere cull).

## Decision 5 - editor surface

- Mesh viewer page: LOD dropdown (Auto/0/1/2/...) + per-LOD triangle
  counts; wireframe toggle already exists.
- Import dialog: the Generate LODs toggle + budget; re-import regenerates.
- Debug: a per-scene debug-draw overlay mode tinting geometry by active
  LOD (rides the existing debug-draw categories).

## Phasing (for when it is scheduled)

- **P0 - meshoptimizer vendoring + cooked-mesh optimization pass.**
  Vertex-cache/fetch/overdraw-optimize every cooked static mesh (no wire
  change - same buffers, better order). Acceptance: identical rendering
  (pixel probe), measured ACMR improvement logged by the cook test.
- **P1 - wire + authored LODs + runtime selection.** Sidecar LOD table,
  importer name-convention collapse, extraction selection + hysteresis,
  component knobs, mesh-page dropdown. Acceptance: authored 3-LOD mesh
  switches by distance on screen; serialize round-trip tests; 1-LOD
  compat load of pre-bump meshes.
- **P2 - auto-generation.** meshopt_simplify chains at import with error
  bounds + dialog knobs. Acceptance: Sponza-class import generates chains;
  triangle-count assertions; error-bound drop case tested.
- **P3 - polish.** Shadow-list coarsest-LOD rule, debug tint overlay,
  per-set instanced selection.
- **P4 - DEFERRED:** skinned LOD chains, per-instance LOD bucketing,
  cross-fade transitions, LOD-aware physics cooking (physics keeps cooking
  from LOD0 - collision fidelity is not a rendering concern).

## Test notes

Doctest in Geometry.Pipeline.Tests (wire, chains, compat load) +
ModelImporter.Tests (name collapse, generation) + a Foundation.Render-side
selection unit test (coverage math + hysteresis, no GPU). Both compilers.
Pipeline targets stay UI-free.
