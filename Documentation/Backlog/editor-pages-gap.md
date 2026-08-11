# Editor Pages — coverage gap & the next editor pass

Status: **audit (2026-07-23).** Grounded in a cross-reference of cooked asset types vs. registered
editor pages vs. importers vs. inspector coverage — measured, not estimated. This is the proposed
focus of the next editor pass.

Related: [editor.md](editor.md), [asset-pipeline.md](asset-pipeline.md).

## The finding

**~21 cooked asset types exist. ~8 have a dedicated editor page. ~13 do not** — and the missing ones
are the *high-value authoring tools* (particle, animation-graph, texture, mesh, skeleton). This is
Draconic's clearest weakness relative to peer engines (Lumix ships ~32 asset-editor plugin classes,
one per type).

**Failure mode is hard, not graceful:** `EditorApplication::OpenInstancePage` calls
`m_context.OpenPage(instance)`, and when no factory is registered for the type it returns null →
*"No editor registered for this asset type."* ([Application.cppm:452-458](../../Code/Draconic/Editor/App/Application.cppm)).
So an unpaged asset **cannot be opened at all** from the Asset Browser — there is no generic
inspector-page fallback (the reflection PropertyGrid exists, but only inside the *scene* inspector for
selected entities/components, not as a standalone asset-editing surface). This makes the gap sharper
than "no bespoke tool": for ~13 types, double-clicking does nothing useful.

## Coverage matrix (measured)

| Asset type | Page | Importer | Notes / what a page needs |
|---|---|---|---|
| Scene / Prefab | ✅ `ScenePage` | — | full |
| Material | ✅ `MaterialPage` | ✅ | full (preview + re-cook) |
| Script / ScriptClass | ✅ `ScriptPage` | ✅ | text editor |
| InputMap | ✅ `InputMapPage` | — | action/binding editor |
| AudioClip | ✅ `AudioClipPage` | ✅ | waveform + playback |
| SoundCue | ✅ `SoundCuePage` | — | variant list |
| UI Document | ✅ `UIDocumentPage` | ✅ | |
| Texture | ✅ `TextureEditorPage` | ✅ `TextureImporter` | import settings + CPU preview (color-space/shape/filters/wraps/mips/aniso + presets) |
| StaticMesh / SkinnedMesh | ✅ `MeshEditorPage` (viewer) | ✅ `MeshImporter` | GPU orbit preview + stats/submesh readout; material-slot picker + save-as-prefab = TODO |
| **Model (import)** | ❌ (opts only) | ✅ | import-options page → save-as-prefab (roadmap already wants this) |
| **AnimationClip** | ❌ | — | clip preview/scrub, events, root-motion toggle |
| **AnimationGraph** | ❌ | — | **blend-tree / state-machine node graph** — the biggest single page |
| **Skeleton** | ❌ | — | bone tree + bind-pose view |
| **ParticleEffect** | ❌ | — | **module stack + live preview** (Lumix's is ~3.7k LOC) |
| **Shader** | ❌ | ✅ `ShaderImporter` | source editor + variant/error surface |
| **Image** | ❌ | ✅ `TextureFileImporter` | source-image inspect (feeds Texture) |
| **AudioBusLayout** | ❌ | ✅ builder | bus-tree editor (routing + per-bus effects) |
| **CollisionShape** | ❌ | ✅ builder | shape preview / primitive params |
| **PhysicalMaterial** | ❌ | ✅ builder | property form (friction/restitution) |
| **Physics** | ❌ | — | (config) |

## Order decision (user, 2026-07-23): bespoke pages FIRST, generic page LAST

Reversed from the original recommendation: build the bespoke authoring tools first (they are the real
value and can't be substituted), and land the generic `ReflectedAssetPage` at the END to sweep up the
remaining property-form long tail + replace the hard `OpenInstancePage` failure.

## Next-pass order (as decided)

1. **Texture page** — ✅ **DONE** (`Code/Draconic/Editor/Texture/`, module `draconic.editor.texture`,
   target `Draconic::EditorTexture`, registered in `Tools/Editor/Main.cpp` via `RegisterTextureEditor`).
   `TextureEditorPage`: a SplitView of a CPU image preview (decoded through the same stb path the cook
   rides; HDR clamped for display; embedded "pixels"-stream textures supported) + a `PropertyGrid` over
   color-space / shape / min+mag filter / wrap U,V,W / generate-mipmaps / anisotropy, plus a Preset row
   (UI / Sprite / 3D / Equirect + Cubemap Skybox). Edits are whole-asset blob-snapshot undo commands;
   Save writes the asset back + `RequestCook(false)`. Headless tests in `Tests/` (factory type-dispatch
   + serialize round-trip + presets); green clang+gcc, DraconicEditor links on both. NOTE: there is no
   "format"/"compression" field on TextureAsset — the GPU format is derived at cook from pixel-format +
   color-space, so the page shows the *derived* format read-out rather than a compression control.
2. **Mesh / Model page** — ✅ **DONE (viewer)** as a `:mesh_page` partition of `draconic.editor.scene`
   (mirrors `MaterialPage`: reuses `EditorCamera` + `ViewportView` + `RenderScene`-to-RT; registered via
   `RegisterMeshEditor` for BOTH `StaticMeshAsset` and `SkinnedMeshAsset`, sibling `editor::Asset`
   subclasses, so a factory each). `MeshEditorPage` = a SplitView of a GPU orbit preview of the cooked
   mesh product (bound by the asset guid through `Resources()->Bind<geometry::StaticMesh>`, lit by a
   seeded sun + procedural sky, under a neutral `CreatePBR` material; a uid watchdog reframes + refreshes
   on cook/hot-reload) + a scrollable stats readout (name / vertex / index / submesh counts / bounds /
   skinned + one line per submesh with its material index). Headless test over the pure `MeshStatLines`
   free fn (Primitives::Cube/Sphere); green clang+gcc, DraconicEditor links both. KEY FINDING that shaped
   scope: **mesh assets carry no re-authorable fields** - per-submesh material INDICES are baked into the
   cooked source at import, and material BINDINGS live on a scene `MeshComponent` (a `materials` array of
   `resource::Ref<Material>` indexed by `SubMesh::materialIndex`), NOT on the mesh asset. So Save is a
   no-op and there is no per-asset material-slot Guid to author. TODO(next pass): a page-local
   preview-material picker per slot (persisted like MaterialPage's preview-mesh pref) + a
   create-entity / save-as-prefab action. **← starting #3.**
3. **ParticleEffect page** — ✅ **P1 DONE** as a `:particle_effect_page` partition of
   `draconic.editor.scene` (mirrors MaterialPage: EditorCamera + ViewportView + RenderScene-to-RT).
   `ParticleEffectEditorPage`: a SplitView of a LIVE GPU preview (the effect plays - the particle
   subsystem injects its component manager into the preview scene via the shared aware registry and
   simulates + renders through the normal provider seam; the emitter entity attaches the asset's live
   `ParticleEffect` via the code-path `SetEffect(fx)`, so edits feed the running sim) + a Restart button
   + a hand-built PropertyGrid for the common knobs (spawn rate, emission mode, emit duration, looping,
   lifetime, start color, start size, gravity). Edits mutate the effect IN PLACE (live for newly-spawned
   particles) + MarkDirty; Save writes the asset + `RequestCook`. Registered via `RegisterParticleEditor`
   with a "Particle Effect" New-Asset creator seeding an upward fountain (`SeedDefaultParticleEffect`,
   unit-tested headless). Green clang+gcc, DraconicEditor links both.
   **P2 DONE - full three-pane authoring tool (mirrors + exceeds Sedulous's particle editor).** Rewrote the
   page from a flat grid into the Sedulous-shaped **three panes**: left = an authoring **tree**
   (`DraggableTreeView` + a `ParticleTreeAdapter`: Effect -> System -> {Emitter, Initializers/, Behaviors/}
   -> module leaves), center = the live preview + a **transport toolbar** (Play/Stop/Restart/Pause + a
   **simulation-speed slider** driving `SceneManager::SetTimeScale`) + a **live stats overlay** (alive/cap
   per-system), right = a **per-node inspector** (only the selected node's fields). Tree rows are a
   `ParticleTreeRow : EditableLabel` that remembers its bound node, so **System rows rename in place** on a
   virtualized tree (the commit handler reads the row's live identity - same pattern as HierarchyView).
   Right-click context menus + drag give **add / delete / reorder** of modules and systems; every structural
   mutation runs through the **UI mutation queue** (`ctx->MutationQueueRef().QueueAction`, never tears down
   the control that raised the event), reselects the target **by identity**, and reattaches the preview.
   COMPLETE FIELD COVERAGE: every `ParticleSystem` field (name via StringEditor, sim mode/space/blend/render,
   max-particles via the new `SetMaxParticles`, sort, soft+distance, prewarm, LOD, full Flipbook, full Trail,
   texture picker via `AssetPickerDialog`), every emitter field (incl. burst count/interval/cycles), every
   module field on all 20 modules incl. **CollisionBehavior's plane/sphere/box arrays** and full 4-component
   RangeFloat2. Curves use the real **`CurveCanvas`** (1-channel Alpha/Rotation/Speed, 2-channel Size) and
   color uses the real **`GradientEditor`**, both writing every key/tangent back + committing undo. **Real
   undo/redo** via coalesced whole-effect blob snapshots (`SerializeEffect`): an in-place edit commits a
   command whose apply is a no-op at push time (if-changed guard) and only re-deserializes on an actual
   undo/redo, so drags never rebuild the grid mid-gesture. Runtime gained `MoveInitializer/MoveBehavior`
   (reorder), `SetMaxParticles` (stream-container move-assign + re-declare), and `RemoveInitializer/
   RemoveBehavior/RemoveSystem/Clear`. Beyond Sedulous: real undo/redo (theirs was unused), stats overlay,
   emission-shape **gizmo** (per-scene debug-draw of the selected system's shape), speed slider, pause.
   Green clang+gcc (Particles 1314, EditorScene 471, DraconicEditor links both).
   **P3 (optional later)**: per-property reset-to-default; sub-emitter-link authoring UI (runtime supports
   `SubEmitterLink` already); a preset/effect library beyond the single fountain default.
4. **AnimationGraph page** — **IN PROGRESS (canvas + inspectors DONE; live preview next).** Built as a
   `:animation_graph_page` partition. PREP: the toolkit `NodeGraphCanvas` (previously only exercised by
   the UISandbox demo) gained first-class **state-machine support**: `ConnectionStyle::StraightNodeToNode`
   (center-anchored rect-clipped edges, lane offsets so A->B + B->A and parallel transitions stay
   distinct, mid-edge direction arrows, straight-segment hit-testing), a `StartLinkFrom` pending-link
   mode + `OnNodeLinkRequested` (the Unity "Make Transition" flow for port-less nodes),
   `NodeGraphNode.IsHighlighted` (active-state ring for live preview), an indexed `OnConnectionDeleting`
   event, and port-less connection validation (parallel duplicates legal). UISandbox/BezierPorts default
   untouched. PAGE: three panes - left Layers+Parameters lists, center the canvas for the selected layer
   (node 0 = Any State pseudo-node; connection index == transition index; right-click adds Clip/1D/2D
   states, node menu = Make Transition / Set Default / Delete with full index remapping, drag persists
   positions), right per-selection inspector (layer name/blend/weight/default; parameter
   name/type/default; state name/speed/loop + clip picker or blend-tree drivers + entries; transition
   duration/exit-time/priority + conditions with param dropdown/op/threshold). Editor-only canvas layout
   lives on the ASSET (`layerStatePositions`/`layerAnyStatePositions` - never the cooked source, per the
   runtime-carries-no-editor-data rule). Coalesced blob-snapshot undo; structural edits via the mutation
   queue. New Asset creator seeds Idle+Speed. Headless tests (seed + asset round-trip incl. layout).
   **D DONE - live preview + clip page.** GRAPH PREVIEW: the center pane split vertically -
   canvas above, a preview strip below (transport: Skeleton picker via AssetPickerDialog,
   Play/Pause/Restart, live status "current state (transitioning)") + a debug-draw viewport. Pick a
   Skeleton -> `source.BuildInto(Resources(), graph)` resolves clips through the cooked DB, an
   `AnimationGraphPlayer` ticks each frame, and the skeleton draws as a bone WIREFRAME
   (`DrawSkeletonWireframe`, shared free fn: ComputeWorldPoses -> parent->joint lines + joint
   crosses + ground grid) in the page's debug-draw-only preview scene. The ACTIVE state gets the
   canvas highlight ring (`IsHighlighted`); the parameter inspector gains a "Live (preview)"
   section driving the player's runtime values (scrub floats/ints/bools, Fire Trigger) - no undo,
   runtime state. The preview graph rebuilds on every edit + skeleton hot-reload (proxy identity
   guard). **ANIMATION CLIP PAGE** (`:animation_clip_page`): cooked-product playback (Bind clip +
   skeleton pick), Play/Pause + normalized scrub slider + time readout, `SampleClip` -> the same
   wireframe; inspector = stats readout (name/duration/tracks) + editable loop flag + full EVENT
   editor (time+name rows, add-at-playhead/remove) with coalesced blob undo. No creator (clips come
   from import). Registered for AnimationClipAsset. Headless tests: graph seed + graph/clip asset
   blob round-trips. Green clang+gcc. Pending: on-screen verify (add to smoke checklist).
5. **Skeleton page** — ✅ **DONE** (`:skeleton_page`): bone TREE (read-only DraggableTreeView over
   parent indices, ContentInset-indented rows) + stats line (`SkeletonStatLines`, headless-tested) |
   bind-pose wireframe viewport (`DrawSkeletonWireframe` over the cooked product's localBindPose;
   selecting a bone draws an orange emphasis cross/sphere at its joint - the wireframe fills the
   caller's world-scratch, so the page reads the position back) | read-only bone info pane (index/
   parent/children/bind TRS). Pointer-identity watchdog refreshes on late cook / hot-reload; Save is
   a no-op (imported, like the mesh viewer); no creator. FIX while here: the graph page crashed on
   open (user repro) - its ctor was missing the router/camera/subsystem init (a patch script had
   failed before writing; only InputRouter::AddSurface actually dereferenced). Root-caused via the
   existing DRACONIC_TEST_OPEN=<guid> headless-open hook under gdb; creator data path pinned by a new
   real-project regression test.
6. **AudioBusLayout page** — ✅ **DONE** (`:bus_layout_page` in `draconic.editor.audio`): the mixer
   editor. LEFT = the bus tree (Master -> Effects/Music/UI + used custom slots parented by name,
   unknown/empty -> Master like the cook) + "+ Add Bus" (first empty slot); RIGHT = the selected
   bus's inspector (volume/mute + lowpass/highpass/delay/reverb; custom buses also rename - children
   follow, duplicates/fixed names refused - re-parent via a dropdown FILTERED by the pure
   `AudioBusWouldCycle` guard, and Remove - orphans re-parent to Master explicitly). Coalesced blob
   undo; Save + recook. KEY FIX the tests caught: the asset gates its custom-slot bank on
   `ar.Version() >= 2`, so undo snapshots MUST ride `BeginVersionedPayload(StaticType())` - a raw
   BinarySerializer reports version 0 and silently DROPS every custom bus. Headless tests: cycle
   guard (direct + transitive) + versioned round-trip.
7. **Image page** — ✅ **DONE** (`draconic.editor.image` / `Draconic::EditorImage`, user request:
   "dedicated editor for image, similar to texture"): the Texture page's SOURCE-side sibling over
   ImageAsset. CPU preview of the decoded file (same io::LoadImage the cook rides; non-RGBA8
   converts through `Image::ConvertFormat` for display) + the one authored field (color-space
   intent, blob-undo with in-place row refresh - no grid rebuild) + read-only source facts
   (file / dimensions / pixel format / data size). Save + recook. Headless tests (factory
   dispatch, registry routing, asset round-trip).
8. **Generic fallback page** — ✅ **DONE** (`draconic.editor.generic` / `GenericAssetEditorPage`).
   KEY FINDING that reshaped the design: the assets are NOT reflected (fields exist only in
   Serialize), so the planned "ReflectedAssetPage" became a **SERIALIZE-DRIVEN form**: a scanning
   serializer runs the object's own Serialize (write mode, with the type's CURRENT data-version
   chain pushed manually so `ar.Version() >= N` gated fields appear) and records every value as an
   ordinal field list (scalars/strings/guids/blobs/array counts); an edit replays Serialize (read
   mode) feeding the recorded values back with ONE field patched. Value-conditional Serialize
   branches are handled: the replay goes inert on shape mismatch (fail-safe) and a post-edit
   re-scan rebuilds the grid when the shape changed (deferred - never mid-scrub). UI: bool/int/
   float/string editors by kind; guids get a canonical-string row (TryParse-validated) + an
   UNTYPED "Pick" button (AssetPickerDialog gained empty-filter-matches-all). Blob + array-count
   rows are read-only/hidden. Blob undo + Save + recook as usual. FALLBACK ROUTING needed no new
   seam: the factory registers `PrimaryType = ISerializable::StaticType()` and the registry's
   nearest-base dispatch does the rest - every bespoke page (concrete type, distance 0) wins;
   everything else (CollisionShape, PhysicalMaterial, Physics config, future types) lands here
   instead of the hard "No editor registered" failure. Headless tests: scan (all kinds +
   version-gated + array labels), patch isolation, conditional shape change, fallback routing.
9. **Shader page — LAST; design DECIDED (user, 2026-07-28), build gated on prerequisites.** The
   shaders-out-of-C++ discussion landed (see shaders.md, task #113): shaders become/stay `.hlsl`
   source assets (engine built-ins lift out of the C++ string banks too), so the page is the full
   text-editor shape — CodeEditView (HLSL lexer + completion, task #111) with DXC diagnostics as
   Error/Warning markers, per-stage tabs, compile-on-edit, and a live preview viewport
   (material-on-mesh via the GPU-page recipe). It is shaders-track P4: build after CodeEditView
   P1+P2 and the shaders-track P1 file lift.

## Reusable seams already in place (so pages are cheap to add)

- **`IEditorPageFactory`** ([Page.cppm:73](../../Code/Draconic/Editor/Core/Page.cppm)) — register a
  factory per type; `OpenPage` routes by type name. Adding a page = one factory + one `:*_page`
  partition (pattern: `MaterialPage.cppm`, `SoundCuePage.cppm`, `InputMapPage.cppm`).
- **`UIEditorPage` / `:ui_page` contract** — all pages produce these; consistent docking/persistence.
- **`PropertyGrid`** — reflected property editing, ready to reuse for #1.
- **Per-viewport `ViewportView` + RenderScene-to-RT** — the mesh/particle/skeleton preview pages get 3D
  preview for free (the scene pages already use it).
- **`SnapshotCommand`** — generic undo for `ISerializable` assets, so the generic page gets undo without
  per-type command code.
- **Importers** already exist for Texture/Mesh/Shader/Audio/UI — the pages are the *settings/preview*
  surface over an import path that already works, not new pipeline.

## Scope note

This is the editor's least-complete axis and the right next focus. It is **breadth** work (13 types),
not a rewrite — the pipeline, cook, inspector, viewport, and page-factory seams all exist. Item #1
(generic reflected page) should land first because it removes the hard failure and covers the simple
long tail in one shot; the bespoke pages (Texture → Mesh → Particle → AnimGraph) are then a sequenced
series, each self-contained.
