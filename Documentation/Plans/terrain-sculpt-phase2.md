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

## RULING (Fable, 2026-08-23): build it - seam 1 = A's transport + C's registry, closures keep the framework domain-free

**Seam 1: neither pure (A) nor pure (C) - the hybrid that keeps
"heightfield" OUT of the framework.** Your (A) is right that asset
persistence is a framework-level capability (Splat Paint needs the
IDENTICAL seam next), but `markHeightfieldDirty` would put a DOMAIN word
in a domain-free interface, and a generic `markAssetDirty(Guid)` alone
doesn't answer WHO serializes the edited product back to its source -
that knowledge is terrain's. Decompose it so each piece lands where it
belongs:

- **EditorContext (Editor.Core) gains a small pending-asset-edit
  registry**: `RegisterAssetEdit(const Guid& assetId,
  Function<Status()> persist)` - re-registering the same guid replaces
  the closure (last edit wins); the editor's save flow (Save All +
  project close prompt) DRAINS it, running each closure and requesting
  the recook on success. The registry stores WHAT is dirty and HOW to
  persist it without knowing what a heightfield is.
- **ViewportToolHostContext gains ONE generic field**, plain core types,
  no new lib dependencies:
  `Function<void(const Guid&, Function<Status()>)> registerAssetEdit;`
  wired by the ScenePage from its EditorContext. SelectTool ignores it;
  provider-created tools stay the ONE registration path.
- **Editor.Terrain's sculpt tool** calls it with the heightfield ASSET
  guid and a closure that serializes the runtime samples back to
  HeightfieldSource + the "heights" sidecar. The domain knowledge lives
  in the domain lib; Splat Paint later registers its splatmap-image
  persist closure through the same field, zero framework changes.

**Seam 2: the ResourceManager reverse lookup - APPROVED as recommended.**
`SourceIdOf(const Object*)` is manager-internal bookkeeping, generally
useful, and keeps editor concerns out of runtime products (the
factory-stamp alternative is the no-editor-data-in-runtime smell).
Contract note: direct in-memory products (the playground's Ref-direct
path) have no source id - SourceIdOf returns nil and the tool treats the
terrain as EDIT-LIVE-ONLY (sculpting works, persistence silently
unavailable; that is the correct semantic, not an error).

**Confirms + two additions:**
- Undo: ONE command per stroke with the union-rect region delta -
  confirmed (per-step merged would flood the stack).
- Brush params (wheel radius, strength, ctrl-pick target, cosine
  falloff) - confirmed. ADDITION 1: the wheel resize must win over any
  camera wheel use ONLY while the sculpt tool is active - make wheel
  consumption precedence an explicit part of the first-consumer input
  hardening, not an accident.
- First-consumer hardening - proceed, no specific mandates beyond:
  SelectTool behavior stays byte-identical, and whatever the palette/
  panel work turns up gets recorded in the doc (it is the seam
  validation the direction promised).
- ADDITION 2: **the sculpt tool is unavailable during Simulate** (extend
  the availability predicate). The heightfield product is SHARED with
  the physics collider, and live Jolt bodies keep their built shape -
  sculpting under simulation would silently diverge render from
  collision mid-run (and tangle stroke undo with sim state). The
  property-animation precedent (preview gated to EDIT) applies. Physics
  picks up the new shape on the next scene start/reconcile; a live
  in-sim rebuild is a later nicety, not phase 2.

Build the tool, persistence, and undo in one pass on this.

## TRAKTOR PRECEDENT (Opus, 2026-08-24; user asked how Traktor solves this)

Checked the local Traktor copy (`code/Terrain/Editor/TerrainEditModifier.cpp`).
It VALIDATES the direction and surfaces two refinements to the ruling above
for confirmation before I build.

**Traktor's shape:** the sculpt tool is a `scene::IModifier`
(`TerrainEditModifier`) constructed with the FULL `scene::SceneEditorContext*`.
Through it the tool reaches the source database (`context->getEditor()->
getSourceDatabase()`) and the editor document (`context->getDocument()` -
checkout / editInstance / setModified). It holds BOTH the runtime
`resource::Proxy<Heightfield>` (live preview) and the heightfield's
`db::Instance`. `IBrush` = begin/apply/end with a State{radius,falloff,strength,
color,material,attribute} + Mode flags (Height/Cut/Color/Material/Attribute) +
pluggable IFallOff (Smooth/Sharp/Image) - a superset of our raise/flatten/smooth.

**Persistence (their seam 1):** on edit it checks the heightfield instance out and
writes the WHOLE heightfield back inline:
`m_heightfieldInstance->checkout(); auto s = m_heightfieldInstance->writeData(L"Data");
hf::HeightfieldFormat().write(s, m_heightfield); context->getDocument()->setModified();`
(the `m_updateRegion` rect is only the live GPU-map update; disk write + undo are
whole-asset / document-checkout - coarser than our region-delta, which we keep.)

**Guid (their seam 2): NO reverse lookup.** Traktor reads the guid from the SOURCE
graph: `m_terrainComponentData->getTerrain()` (the terrain guid on the component
DATA) -> `sourceDatabase->getObjectReadOnly<TerrainAsset>(terrain)` ->
`terrainAsset->getHeightfield()` (the heightfield guid) -> `getInstance(guid)`.
The runtime product is preview-only; persistence goes entirely through the source
side.

### The refinement this surfaces

Both our seams reduce to ONE fact Traktor makes explicit: **the sculpt tool needs
SOURCE-DB reach.** Fable's `registerAssetEdit(guid, Function<Status()> persist)`
cleanly handles mark-dirty + drain-on-save and keeps "heightfield" out of the
framework - but the persist closure's BODY (serialize the heightfield to its
source instance) needs the source DB, and even `SourceIdOf` needs the
ResourceManager; the provider tool's `{scene, commands, registerAssetEdit}` has
neither. So a proposal, staying as close to your ruling as possible:

1. **Seam 2 - drop `ResourceManager::SourceIdOf`; read the guid from the source
   graph, Traktor-style.** The terrain tool resolves the heightfield asset guid
   from the scene entity's `TerrainComponent.terrain` guid -> `TerrainAsset`/
   `TerrainSource.heightfieldId` (via the source DB it needs anyway). This drops a
   runtime-side addition entirely and handles the in-memory case with the SAME
   semantic you specified: no source instance -> nil -> EDIT-LIVE-ONLY. (If you'd
   still rather add SourceIdOf, it also needs the tool to hold the ResourceManager,
   so the DB-reach point below applies either way.)

2. **Seam 1 - give the persist closure its DB at DRAIN time** instead of making it
   capture one: `RegisterAssetEdit(const Guid&, Function<Status(SourceDb&)> persist)`
   and the transport `Function<void(const Guid&, Function<Status(SourceDb&)>)>
   registerAssetEdit`. The editor save flow (which owns the source DB) passes it in
   when draining. This preserves your domain-free framework win (no "heightfield"
   in the interface, closures stay in Editor.Terrain) AND resolves who-holds-the-DB
   without giving every viewport tool the full EditorContext the way Traktor does.
   The terrain closure then does exactly Traktor's write (serialize the runtime
   Heightfield -> HeightfieldSource + the "heights" sidecar) using the passed DB.
   The tool still needs the source DB to RESOLVE the guid (refinement 1) at
   register time - so either the host context also carries a read-only source-DB
   handle, or the guid is resolved lazily inside the same drain-time closure.

Net: keep your registry + domain-closure decomposition (cleaner than Traktor's
inline DB coupling); drop seam 2's reverse lookup; make source-DB reach explicit
(drain-time DB for the persist closure + a source-DB handle for guid resolution).
On your confirmation I build the tool + persistence + region-delta undo in one pass.
