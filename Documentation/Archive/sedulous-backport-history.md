# Draconic -> Sedulous back-port worklist

> ARCHIVED 2026-09-01: still-open items live in Documentation/Plans/week-2026-09-05.md
> ("Backlog folder absorbed"). This archive keeps the full detail.


Raw per-lib commit lists for `cbfe1d49..HEAD` (no triage/classification — review each yourself).

- Drop-off (last back-port): `cbfe1d49` - "UIToolkit: docking a panel into a tab group activates its tab" (2026-07-11)
- Range: `cbfe1d49..HEAD`
- In scope (14 libs): Xml, Fonts, Image, RenderGraph, VG, Model, VFS, Shaders, Geometry, RHI, Animation, Materials, Texture, UI
- Separate track (full fresh re-port, not commit-based): Shell
- Off-list: Render, Graphics, Scene, Input, Net, Core, Runtime, Resource, Profiler, Content, Settings, Project, Editor, ModelImporter, Script, GUI, Imgui

## Xml
- [ ] `0fcc7e2c`  Tests: redirect scratch data to a gitignored .test-scratch/ dir
- [ ] `130e739e`  Style: clang-format the entire Code/ tree (format-only)
- [ ] `d4945296`  Xml: keyed reads scan forward from the cursor, not FirstChild

## Fonts
- [ ] `0fcc7e2c`  Tests: redirect scratch data to a gitignored .test-scratch/ dir
- [ ] `130e739e`  Style: clang-format the entire Code/ tree (format-only)
- [ ] `a7745766`  Fonts/UI: shared text-truncation helper + ellipsis for buttons, labels, tiles

## Image
- [ ] `0fcc7e2c`  Tests: redirect scratch data to a gitignored .test-scratch/ dir
- [ ] `130e739e`  Style: clang-format the entire Code/ tree (format-only)
- [ ] `e77900be`  Editor: builder API v2 - VFS sources, versions, dependencies, registry

## RenderGraph
- [ ] `0fcc7e2c`  Tests: redirect scratch data to a gitignored .test-scratch/ dir
- [ ] `130e739e`  Style: clang-format the entire Code/ tree (format-only)
- [ ] `126eb84f`  Render: fix viewport-resize transient-texture leak + harden RG allocation
- [ ] `243fce76`  Update all callers for RHI allocator migration

## VG
- [ ] `0fcc7e2c`  Tests: redirect scratch data to a gitignored .test-scratch/ dir
- [ ] `130e739e`  Style: clang-format the entire Code/ tree (format-only)
- [ ] `d03c8fd7`  VG/UI: viewport seam - split-screen scene HUDs draw in their sub-rect
- [ ] `243fce76`  Update all callers for RHI allocator migration

## Model
- [ ] `0fcc7e2c`  Tests: redirect scratch data to a gitignored .test-scratch/ dir
- [ ] `130e739e`  Style: clang-format the entire Code/ tree (format-only)
- [ ] `028fff00`  Model: cooked-model runtime types move out of the importer (tooling)
- [ ] `0f2d30ae`  Model: loader tangent + material fidelity
- [ ] `2929ee08`  Geometry: Float4 vertex tangents with handedness + per-mesh uid

## VFS
- [ ] `0fcc7e2c`  Tests: redirect scratch data to a gitignored .test-scratch/ dir
- [ ] `130e739e`  Style: clang-format the entire Code/ tree (format-only)
- [ ] `50f3c893`  VFS: writable Move + DeleteDirectory capabilities (native impl)
- [ ] `c662bed1`  VFS: NativeFileSystem implements the watchable seam (stat-sweep)
- [ ] `128a4789`  VFS: IStatFileSystem capability, implemented by NativeFileSystem

## Shaders
- [ ] `0fcc7e2c`  Tests: redirect scratch data to a gitignored .test-scratch/ dir
- [ ] `130e739e`  Style: clang-format the entire Code/ tree (format-only)
- [ ] `b11a902a`  Export: stage DXC beside the player so a relocated dist can compile shaders
- [ ] `0aa1a21e`  Shaders: migrate Compiler to IAllocator with self-deletion
- [ ] `e77900be`  Editor: builder API v2 - VFS sources, versions, dependencies, registry

## Geometry
- [ ] `0fcc7e2c`  Tests: redirect scratch data to a gitignored .test-scratch/ dir
- [ ] `130e739e`  Style: clang-format the entire Code/ tree (format-only)
- [ ] `2929ee08`  Geometry: Float4 vertex tangents with handedness + per-mesh uid
- [ ] `aadf9f3f`  Geometry: StaticMeshFactory builds skinned products as SkinnedMesh
- [ ] `84612ad5`  Geometry: Cylinder/Cone/Torus primitives (Sedulous port)
- [ ] `e77900be`  Editor: builder API v2 - VFS sources, versions, dependencies, registry

## RHI
- [ ] `0fcc7e2c`  Tests: redirect scratch data to a gitignored .test-scratch/ dir
- [ ] `130e739e`  Style: clang-format the entire Code/ tree (format-only)
- [ ] `9b88f8cc`  RHI/Vulkan: log GPU texture allocation failures
- [ ] `126eb84f`  Render: fix viewport-resize transient-texture leak + harden RG allocation
- [ ] `999defd1`  RHI/Validation: migrate all allocations to IAllocator
- [ ] `3713fc33`  RHI/Null: migrate all allocations to IAllocator
- [ ] `b9ccc2b8`  RHI/DX12: migrate all allocations to IAllocator
- [ ] `cdf50863`  RHI/Vulkan: migrate all allocations to IAllocator

## Animation
- [ ] `0fcc7e2c`  Tests: redirect scratch data to a gitignored .test-scratch/ dir
- [ ] `130e739e`  Style: clang-format the entire Code/ tree (format-only)
- [ ] `8ac3fbe0`  Audio/Physics/Script/Animation: rename Components.cppm to <Module>Components.cppm
- [ ] `f224d46a`  Animation: rename files to their primary type + reflect-move + aliases
- [ ] `70579b04`  Style: conform Animation to .clang-format (format-only)
- [ ] `6ce023a3`  Animation: skeletal + graph components on resource refs
- [ ] `e77900be`  Editor: builder API v2 - VFS sources, versions, dependencies, registry
- [ ] `9613463c`  Subsystems: reflect editable components (render / animation / particles)

## Materials
- [ ] `0fcc7e2c`  Tests: redirect scratch data to a gitignored .test-scratch/ dir
- [ ] `130e739e`  Style: clang-format the entire Code/ tree (format-only)
- [ ] `45a370f6`  Materials: per-material sampler address modes imported from the source asset
- [ ] `cf131896`  Materials: default sampler wraps (glTF spec default) + preview-pref restore fix
- [ ] `75f1d92b`  Materials: new forward uniforms, in-memory upgrade, unlit preset, lifetime seams
- [ ] `2929ee08`  Geometry: Float4 vertex tangents with handedness + per-mesh uid
- [ ] `243fce76`  Update all callers for RHI allocator migration
- [ ] `0149a226`  Materials: drop leftover TEMP albedo bind-group diagnostics
- [ ] `469d4084`  Materials: self-contained cooked textures + reload-safe identity
- [ ] `e77900be`  Editor: builder API v2 - VFS sources, versions, dependencies, registry

## Texture
- [ ] `0fcc7e2c`  Tests: redirect scratch data to a gitignored .test-scratch/ dir
- [ ] `130e739e`  Style: clang-format the entire Code/ tree (format-only)
- [ ] `a2e0da37`  Editor: mesh envelopes and source copies flush on the worker too
- [ ] `e41054e3`  Editor: import's bulk stream writes flush on the worker too
- [ ] `60b29247`  Editor: model imports run their heavy phase on the job worker
- [ ] `2894ea3c`  Editor: pre-import options dialog (Sedulous ImportDialog lineage)
- [ ] `e9e054af`  Texture: import HDR skies and cubemaps by drag-drop
- [ ] `cc4cba08`  Texture: cube-aware runtime factory + product identity
- [ ] `243fce76`  Update all callers for RHI allocator migration
- [ ] `36fd05a0`  Texture: embedded-pixels assets + the texture file importer
- [ ] `e77900be`  Editor: builder API v2 - VFS sources, versions, dependencies, registry

## UI
- [ ] `4cca9591`  Windows: fix build errors (AngelScript asm, getenv, SDL_main, SIMD)
- [ ] `0fcc7e2c`  Tests: redirect scratch data to a gitignored .test-scratch/ dir
- [ ] `130e739e`  Style: clang-format the entire Code/ tree (format-only)
- [ ] `547b6a18`  UI: canonicalize the last uirt alias in UIApplication (sec 2.2)
- [ ] `ac8d77d1`  UI: fix draconic::runtime nested-namespace clash in UIApplication
- [ ] `1a95d559`  UI: canonicalize namespace aliases (sec 2.2)
- [ ] `2231caf1`  Scene: SceneSubsystem becomes a pure registry - no default manager (game-instance.md §11 final)
- [ ] `d59032c9`  Editor: script debugger UI - breakpoint gutter + debugger panel + PIE wiring
- [ ] `2fc79680`  Render/UI: WorldUI category - panels draw POST-tonemap, colors match the HUD
- [ ] `efdbce90`  UI: canvas roots are hit-transparent - the kiosk was unreachable (user report)
- [ ] `9ec24e6f`  UI: world-panel center-aim in capture mode + kiosk background (user report)
- [ ] `acf506ef`  UI: the world tier - ui.WorldPanel (RT quad, ray-interactive, unlit)
- [ ] `9b8c8a2c`  UI: scene-bound input routing, editing-page RT canvases, material auto-bind
- [ ] `3c35f15f`  Merge branch 'worktree-agent-a9815ea696e6a513f'
- [ ] `49b3e25e`  UI: project-default UITheme + built-in GameLightTheme (theme variations)
- [ ] `86fb99a4`  UI: RenderTexture canvas mode (offscreen per-canvas targets via a host-encoder seam)
- [ ] `93acc9a4`  UI: keyboard + text input into the game tier (event-first provider seam, IME lifecycle)
- [ ] `bb04d3d6`  UI: canvas scaler - ReferenceResolution lays out at the reference size and scales to fit
- [ ] `0d57b938`  UI: canvas order-stacking (per-canvas hosts, sorted scene roots, despawn sweep)
- [ ] `d03c8fd7`  VG/UI: viewport seam - split-screen scene HUDs draw in their sub-rect
- [ ] `a78ec94b`  Render/UI: adopt the two-tier overlay roles (ISceneOverlay + IScreenOverlay)
- [ ] `8c7b978d`  Editor/UI: UIDocumentPage with live preview through the runtime context
- [ ] `5860598f`  Game-UI P2: gamepad focus navigation
- [ ] `84fb444c`  Game-UI: an empty screen-overlay layer must not swallow clicks
- [ ] `1d7d3eef`  Game-UI P2: billboards, per-scene overlays, and the scene-less screen tier
- [ ] `2fa3bc7f`  Game-UI: cook-time warnings for silently-dropped markup (P2)
- [ ] `fc27d5de`  Game-UI: fix the HUD layout + font path (the vertical-bar bug)
- [ ] `64b82430`  Game-UI P1: draconic.ui.subsystem - screen tier, consumption, GameTheme
- [ ] `f78ffc89`  Game-UI P1: UIDocument/UITheme asset families (validated-text cook)
- [ ] `244b355c`  Editor: Game viewport clears while idle (defined layout + no stale frame)
- [ ] `4abd862f`  Shell/Viewport/Editor: play-in-editor polish - touch via InputSurface, preview resolutions, pause, script-error notices
- [ ] `fcdf0434`  UI: Disabled dominates interaction flags in StateListDrawable lookup
- [ ] `c59058d9`  Editor: Revert to Prefab disables when clean, with a row tooltip
- [ ] `fa1c30ed`  UI/Editor: DialogResult::None buttons are caller-managed; inline Save As errors
- [ ] `26b6666f`  Toolkit: SetDisplayName updates the built row's label live
- [ ] `4115380e`  Editor theme: Graphite & Orange palette (was amber)
- [ ] `5f7785d9`  UI theme: per-type font sizes + toast/menu/tree polish
- [ ] `a7745766`  Fonts/UI: shared text-truncation helper + ellipsis for buttons, labels, tiles
- [ ] `8dae7a72`  UI styling: CSS source-order tie-break in rule resolution
- [ ] `c99e857b`  UI: round the two remaining direct-draw highlights in the rounded theme
- [ ] `884b89f4`  Editor: Graphite & Amber theme + rounded-theme polish
- [ ] `e812dc61`  UI: TabView tab-strip overflow scrolling (wheel + scroll-into-view)
- [ ] `08666a39`  UI toolkit: ToastHost notification overlay
- [ ] `f04c657e`  UI toolkit: property-row tooltips + visibility, dock tab-strip scrolling
- [ ] `e521f686`  UI: tooltips resolve the nearest ancestor with content
- [ ] `c46bf73d`  UI: GridView GetActiveView + OnItemKeyDown (ListView parity)
- [ ] `eecf44b2`  Toolkit docking: activation follows presses, close requests are veto-able, selected tabs get the accent strip
- [ ] `a20dfbad`  UI: mouse events carry modifiers; plain click replaces selection; dialog text wraps
- [ ] `243fce76`  Update all callers for RHI allocator migration
- [ ] `2cd66e88`  Toolkit: property editors tag their fields with a style class
- [ ] `63156cd1`  UI: NumericField selects all on focus (type-to-replace)
- [ ] `7509ec42`  UI: TreeView::SetAdapter(nullptr) detaches instead of crashing
- [ ] `0ad07a7d`  Toolkit: Float2/Float4 property editors; rename Vector3Editor to Float3Editor
- [ ] `a630efb1`  Core: Quaternion euler conversions (FromYawPitchRoll / ToYawPitchRoll)
- [ ] `a12f0e97`  UI: Return dispatch-first, list click-to-focus, drop-zone scroll fixes, key-map gaps
- [ ] `732037da`  UI: TreeView re-SetAdapter UAF fix + DraggableTreeView drop-INTO zones
- [ ] `35254f61`  Editor: render all scene pages in ONE renderer frame bracket

