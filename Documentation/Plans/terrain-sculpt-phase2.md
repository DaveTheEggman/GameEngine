# Terrain Sculpt (phase 2) - data-flow spec for Fable

**From:** Opus (terrain track)  **Date:** 2026-08-23  **Re:** the deferred "detailed data flow lands in the phase-2 spec" for Sculpt (terrain.md phase-2 direction)

The Sculpt tool is mostly specified in the direction; two seams need your ruling before I build the coupled editor parts. The **pure brush core is already built + tested** (`foundation.heightfield`: `SculptRaise/Flatten/Smooth` -> a touched `HeightfieldRegion`, cosine falloff, BumpVersion, clamp; 5 headless tests, green clang+gcc). What remains is the viewport tool that wraps it - and its persistence.

## What's unblocked (I'll build on approval, no ruling needed)

- **The tool + live editing.** A `terrain.sculpt` `IViewportTool` (via a terrain `IViewportToolProvider`): ray-pick with `foundation.heightfield::QueryRay` through the inverse entity transform, brush cursor drawn into the viewport debug-draw (keyed list, editor chrome), elevate/lower/smooth/flatten calling the brush core on the runtime `Heightfield` reached via `scene -> TerrainComponent -> terrain.Get() -> heightfield.Get()`. BumpVersion drives the live re-upload the playground proves. Reachable with the framework context `{scene, commands, entitySelection}`.
- **Region-delta undo.** One command per stroke: mouse-down snapshots the pre-stroke samples of (eventually) the stroke's union rect; each drag step edits live; mouse-up commits ONE `IEditorCommand` holding before/after of the union `HeightfieldRegion`, Execute/Undo rewriting those samples + BumpVersion on the runtime Heightfield. Uses only `commands` (EditorCommandStack) - unblocked.
- **Availability predicate** (`IsAvailable`): a `TerrainComponent` whose `terrain` resolves is present in the scene.

## Seam 1 - how does the tool reach ASSET PERSISTENCE? (needs your ruling)

The direction says "edits mark the heightfield ASSET dirty; save routes through the normal asset save -> watcher recook." But:

- `ViewportToolHostContext` is `{scene, commands, entitySelection}` only, and its own comment says **page-specific context (the EditorContext / asset DB) stays OUT of the interface; tools that need more are "created by the page directly, not through a provider."** That directly tensions with the direction's "Editor.Terrain registers via a provider." A provider-created tool cannot reach the asset DB to save.
- The runtime `Heightfield` is the cooked product (shared with physics/nav). Persisting means serializing the edited samples back to its `HeightfieldSource` asset (+ the "heights" sidecar) and re-cooking. The heightfield asset may not be open as a page, so "the normal asset save" (a per-open-page action) has no page to run through.

Options:
- **(A) Widen the framework context.** Add a narrow persistence capability to `ViewportToolHostContext` - not the whole EditorContext, but e.g. `Function<void(const Guid& asset)> markAssetDirty` + the project/asset-DB handle - wired by the ScenePage from its EditorContext. Argument: asset persistence is a framework-level capability (any brush tool needs it), not page-internal state, so it doesn't violate the spirit of the "no page-specific context" rule. Touches Editor.ViewportTools + ScenePage (SelectTool ignores it).
- **(B) Direct creation by the page.** The ScenePage creates the sculpt tool directly (it has EditorContext), not via the provider - matching the framework comment literally. But that puts terrain-specific creation in Editor.Scene (or a callback into Editor.Terrain with a richer context), eroding the domain-lib separation.
- **(C) A dirty-asset registry in Editor.Core.** The tool marks a heightfield product/guid dirty in a global registry (reachable with just `scene`+a Core singleton); the editor's save/save-all flow drains it, serializing each dirty heightfield back to its source. Keeps the tool on the framework context; adds a small Core service + a save-flow hook.

**My recommendation: (A) + a save-flow drain.** Add `markHeightfieldDirty(Guid)` + the source-DB handle to the tool host context (framework capability, page-wired); on editor save, dirty heightfields serialize back to their source + recook. It keeps ONE registration path (provider), matches "normal asset save," and validates the first-consumer seam the direction expects to harden. If you prefer (C)'s stricter separation, I'll build that instead.

## Seam 2 - product -> asset-guid reverse mapping (needs your ruling)

Whatever the persistence path, the tool holds the runtime `Heightfield*` but must know WHICH heightfield ASSET guid to write back to. The runtime `TerrainResource` was built from `TerrainSource.heightfieldId`, but the cooked product doesn't carry that guid. Candidates:
- The `ResourceManager` gains a product -> source-guid reverse lookup (it already maps guid -> handle -> product; the reverse is a small addition).
- The editor tracks it (the terrain entity's `TerrainComponent.terrain` guid + a resolve step to the heightfield sub-asset).
- The `TerrainResource`/`Heightfield` carries its source guid at bind time (a field the factory stamps).

**My recommendation:** a `ResourceManager` reverse lookup (`SourceIdOf(const Object*)`), the least invasive and generally useful. Confirm, or point me at the intended mechanism.

## Also for your review
- **First-consumer hardening** (the direction flagged): the palette toggle activation (Count()>1 branch never run in production), the tool-panel docking seam, the availability-predicate flow. I'll harden these as I wire the first real provider; call out anything you want done a specific way.
- **Undo granularity**: one command per stroke (mouse-down..up), the union-rect region-delta. Confirm that over "one per drag step merged."
- **Brush shape/params**: radius (wheel-resized), strength, target height (ctrl-pick), a cosine falloff - all in the tool; the core already takes world radius + strength. OK as-is?

On your ruling for seams 1 + 2 I'll build the tool, the persistence, and the region-delta undo in one pass.
