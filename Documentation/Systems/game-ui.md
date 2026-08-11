# Draconic Game UI — draconic.ui as a runtime subsystem (design)

Status: P1 + P2 SHIPPED (2026-07-18). P1: f78ffc8 assets + 64b8243 subsystem +
sample. P2: 2fa3bc7 cook-time warnings (unknown attrs/elements), 1d7d3ee billboards +
scene-less screen tier (Push/RemoveScreenOverlay; overlay layer topmost, survives scene
swaps), 84fb444 empty-overlay hit-test fix, 5860598 gamepad navigation
(dpad/left-stick MoveFocus + hold-repeat, South=activate, East=Escape; pad is NOT a
consumption class), 8c7b978 UIDocumentPage - text editing + LIVE preview rendered
through the RUNTIME context's UISubsystem (CreatePreview/RenderPreview seam; game
fonts/theme/VG path; dedicated RootView never on the screen root) into a ViewportView
offscreen target. P1 deviations now closed except: canvas order-stacking + scaler
transform + text input to UI (arrives with a focused-EditText consumer).
OVERLAY-ROLES REFACTOR SHIPPED (a78ec94, 2026-07-18, user-directed after reviewing the
Sedulous IScreenOverlay/IScreenRenderer/IPipelineOverlay/ISceneRenderer contracts): the
render layer now has the two-tier overlay coordination model - `ISceneOverlay` (per-view,
drawn inside the compose after post / before debug draw, matched by SceneKey, given the
view's REAL camera) + `IScreenOverlay`/`IScreenRenderer` (window-space registry; hosts make
one generic RenderOverlays call per target; DefaultApplication and GamePage name no UI).
UISubsystem implements both: per-scene roots (billboards + canvases; scene isolation is
structural; billboards project per view - correct in editor viewports/camera previews) +
the scene-less screen root. Input picks the ActiveInputRoot per frame (occupied screen
tier = modal > pointer-hit root > first scene root with content for pad-only nav). The VG
ring resets once per UI frame (call-per-draw reset would clobber same-frame slices).
SPLIT-SCREEN DONE (d03c8fd, user-approved VG change): VGRenderer sub-rect Render
overload (viewport offset + all scissors clamped to the rect; pure ComputeScissor
helper, tested) - scene HUDs lay out per half and billboards project in viewport px.
TRACK COMPLETE (agent, merged after audio P3): per-surface scene binding
(SetSourceProvider(provider, sceneKey) + UnboundInputScenePolicy{AllScenes,
ScreenTierOnly}; GamePage binds on Play; the editor's embedded context runs
ScreenTierOnly - editing/Simulate HUDs are WYSIWYG but NOT interactive, rationale at the
set-sites) - BOTH known edges closed; RT canvases render in editing pages (once-per-UI-
frame guard); RT->sprite/decal auto-bind on the canvas entity (rebind on resize, unbind
before target free) + RT-root registry sweep (dangling-registration fix). Remaining in
the track: ONLY the world tier (design conversation) + script bindings (Wren handles) +
the code-editor control wish.
Next: P3 (RenderTexture canvas mode, declarative script bindings, theme variations)
+ the world-tier design conversation (right-or-deferred; VG consult first).

## 0. Locked decisions (2026-07-18)

1. **Core UI only, never the toolkit.** draconic.ui.toolkit is TOOLING (editor property
   grids etc.); games consume the core control set. Nothing in this subsystem may import
   the toolkit.
2. **One UIContext, owned by the subsystem.** The UISubsystem owns a UIContext; the theme
   is set ON the context. A new built-in **GameTheme** (code theme, alongside Dark/Light/
   RoundedDark) is the default; game code overrides the theme per-context or per-control —
   mechanics that all exist today (UISandbox demonstrates every piece). The cooked
   UITheme-asset story layers ON TOP of this later; no project-settings default needed
   for P1.
3. **Input consumption confirmed**: actions-only gating (no IsMouseOverUI polling API),
   with SEPARATE consumption classes (pointer/keyboard/text) — a menu eating the mouse
   does not mute gamepad movement actions. Raw device facades stay unfiltered.
4. **Tier shape: follow Sedulous — EXCEPT input.** The Engine.UI three-tier chain (screen
   overlay -> per-scene HUD -> billboards -> world panels) and its UI pass are the
   reference shape; a deep reference read of Sedulous.Engine.UI precedes P1 and refines
   §3. Its INPUT however is the part Sedulous got wrong and hacked together (user-
   confirmed) - nothing of its routing is copied. Input is built on OUR stack: the tagged
   shell event stream -> InputSurface/InputRouter -> action layer with the consumption
   mask (§0.3, §3.3). Sedulous's input code is read only to catalog what to avoid.
5. **Rendering: a registered overlay STAGE.** The renderer's stage/category registration
   is the seam — the UI pass registers into an overlay stage (after post) rather than
   growing bespoke hooks in the player.
6. **Script wiring PARKED** (both the entity-addressed v1 and name-based variants):
   blocked on entity handles in Wren, same as physics' per-entity APIs. P1 menus wire
   through C++ handlers.
7. **UIDocumentPage stays in scope** with an honest v1: the EXISTING multi-line EditText
   as the text pane (no dedicated code-editor control yet) + live preview panel to the
   side.

8. **World panels: done RIGHT or DEFERRED - no stopgap.** Sedulous's per-panel-RT +
   per-panel-sprite mode (the confirmed wart) will NOT be ported as a v1. The world tier
   ships only once a proper design is agreed - candidates: Flax-style projected
   direct-draw (requires the decision-9 VG consultation first) or a batched RT/atlas
   design. Until then the world tier is OUT of the phasing; billboards (which are done
   right in the reference - batched, one draw) are unaffected.
9. **VG renderer is OFF-LIMITS without consultation.** The expectation is that game-UI
   needs NO VG renderer modifications. If implementation ever appears to require touching
   it (world-tier transforms, depth states, anything), STOP and consult first - do not
   change it unilaterally. (P2's world tier gets planned against this constraint; the
   RT+sprite fallback and the existing per-slice projection seam are the first resorts.)

Verified in code (2026-07-18, retired from open questions): the `.sml`/`.sss` loaders are
fully ported WITH tests (MarkupLoader + MarkupRegistry + MarkupLoaderTests; Styling/Parser
with 62 SSS test cases; VFS resource provider) — no loader porting hides in P1. The VG
renderer already routes vertex transforms through a per-slice Float4x4 projection cbuffer,
so IF a world-tier transform path is ever wanted it is small — but per decision 8 that is
a consult-first conversation, not a P2 default.

## 1. Goals

- Games show UI three ways: **screen-space HUD/menus**, **world-space panels** (UI on 3D
  surfaces), and **world-anchored billboards** (nameplates/health bars) — all authored as
  **assets** and placeable via components/prefabs.
- UI **documents and themes are cooked assets** with hot reload — the single biggest
  Sedulous gap (its `.sml`/`.sss` loaders existed but the engine subsystem never used them;
  every sample built UI imperatively and no importer/resource type existed).
- **One host implementation** across game, sandbox, and editor (Sedulous shipped two
  divergent ones).
- Input routing that is **unambiguous** (UI-consumed input never reaches gameplay) and works
  identically in the player and in play-in-editor (Sedulous's world-UI input was broken
  under editor-managed input; viewport forwarding was mouse-only — our
  `InputSurface`/`InputRouter` already forwards all devices).

## 2. Reference survey — conclusions

**Sedulous** (`Engine.UI`): three-tier chain (screen overlay → per-scene HUD → billboards →
world panels) with first-consumer routing — good bones, we keep the tier model. World
panels = render-to-texture + sprite (one RT + material per panel, no batching). Billboards =
screen-space anchored to projected world points with distance scaling — cheap and good,
keep as-is. Avoid: dual hosts, markup-not-integrated, no assets, `IsMouseOverUI` polling as
the only gameplay gate, world-UI input hand-rolled against the raw mouse.

**Flax** (`UICanvas`/`UIControl`): the cleanest ECS bridge — a canvas component owning one
retained-mode root. **World-space UI is projected direct-draw** (widget tree drawn straight
into the scene target with a world×view×proj transform + optional scene-depth test), not
RT — cheaper, crisper, and the ray→UI mapping is trivial (OBB intersect → inverse world
matrix → local 2D). Pluggable `CalculateRay` for split-screen. **`CanvasScaler`** per-canvas
DPI policy (constant-pixel / physical / reference-resolution-with-curve). Gamepad nav =
explicit per-control focus neighbors + geometric fallback, driven by named input actions
with hold-repeat. Weakness: theme is a global singleton, not an asset.

**Godot** (`Control`/`Viewport`): the input contract to copy — strict phased dispatch
(`game _input` → **GUI** → `game _unhandled_input`) with `set_input_as_handled`; per-control
`mouse_filter` STOP/PASS/IGNORE; **`Theme` as a standalone swappable Resource** with
type→item maps + variations + per-instance overrides; editor-connectable named signals.
World-space = SubViewport RT with manual picking — more boilerplate than Flax; not copied.

**Traktor Spark**: the asset split — source asset → pipeline → immutable cooked resource →
factory → separate player/renderer. Authoring-time parsing stays out of the runtime.

### 2.1 Sedulous.Engine.UI deep-read findings (2026-07-18; the authoritative reference)

Source: /home/robert/Dev/Beef/SedulousEngine/Code/Engine/Sedulous.Engine.UI. What the doc
called "three tiers" is really FOUR, over TWO render mechanisms:

| Tier | Sedulous class | Owner | Draw path |
|---|---|---|---|
| 1 Screen overlay (window) | ScreenUIView | subsystem (global) | screen-overlay pass: one shared Load pass onto the HDR target, AFTER 3D, BEFORE blit |
| 2 Per-scene HUD | UISceneModule (Order 100) | per scene | pipeline OverlayPass (after particles, before debug passes) |
| 3 Billboards | BillboardUIComponentManager (Order 50) | per scene | same pipeline OverlayPass - ONE shared root, ONE VG batch/draw for ALL billboards |
| 4 World panels | UIComponentManager + WorldUIPass | per scene | one RT pass PER DIRTY panel (Clear/Store) -> SpriteComponent w/ MaterialInstance; graph-scheduled before transparents via RequireReadableAfterWrite |

Structure worth copying:
- **UIContext topology is a TREE, not one context**: 1 global (owns the THEME/stylesheet)
  + 1/scene HUD + 1/scene billboards + 1/world panel; every non-global context BORROWS the
  global's stylesheet (shared, refcounted). Our locked "subsystem owns a UIContext, theme
  on the context" = the global one; tier contexts share its stylesheet the same way.
- **Two-stage scene init**: OnSceneCreated constructs + injects (no GPU/UI resources);
  OnSceneReady (second pass, after the render pipeline exists) adds the world-UI pass and
  Initialize()s + registers the HUD/billboard overlays. This is how UI depends on the
  pipeline regardless of subsystem registration order - we need the same two-phase hook
  (or equivalent ordering guarantee) in draconic.scene's ISceneAware.
- **Update-vs-render split**: subsystem Update (order 400, pre-render) does input +
  BeginFrame (mutation drain/animation ticks) for ALL contexts + layout of the SCREEN view
  only; HUD/billboard/world LAYOUT runs at draw time against the live RenderView size.
- **Billboard math**: worldPos -> clip -> NDC -> screen px written into a shared
  AbsoluteLayout's child params; behind-camera = park at (-10000,-10000) (clipped + unhit,
  no tree churn); distance scale = clamp(referenceDistance/distance, min, max) applied as
  a 2D ViewTransform scale. Cheap and batched - port as-is.
- **World panels are dirty-gated**: a PostUpdate pass collects components whose MarkDirty
  was called (interaction/hover marks dirty); only those re-render their RT.
- **No pause gate**: Sedulous ticks UI always. Our decision: UI updates with UNSCALED dt
  (menus animate while the game is paused/time-scaled) - deliberate, matches by accident.
- **VG renderer needs nothing new**: every tier feeds pixel width/height to the existing
  Prepare/Render slice API (screen + RT alike). Consistent with locked decision 8.

Confirmed warts (all avoided by our locked decisions): dual input hosts with a polling vs
dispatch split; world-panel input hand-rolled against the raw mouse ONLY on the polling
path (editor world-UI input simply missing); IsMouseOverUI polling as the gameplay gate;
markup loaders shipped but never consumed by the subsystem; no UI assets anywhere; world
panels = per-panel RT + per-panel sprite material, unbatched (billboards show the batched
counter-example).

## 3. Architecture

```
draconic.ui / .toolkit / .runtime / .viewport / .shell / .vfs    (EXIST - the framework)
draconic.ui.resource     UIDocument + UITheme cooked resources + factories        (NEW)
draconic.ui.editor       UIDocumentAsset/UIThemeAsset + builders + importer       (NEW)
draconic.ui.subsystem    UISubsystem + canvas components + render/input bridges   (NEW)
```

- **One host.** `UISubsystem` composes the SAME `UIContext`/root/VG machinery the editor's
  UIHost and UISandbox use — thin per-tier wrappers, no parallel implementation.
- **Markup + stylesheets**: the ported `.sml` (XML view-tree) and `.sss` (stylesheet)
  loaders in draconic.ui become the ASSET payload formats. (Verify the port carried
  MarkupLoader/StyleSheetLoader + the VFS resource provider; UISandbox exercised them
  upstream. Anything missing gets ported as part of P1.)

### 3.1 Tiers and components

- **Screen tier** (per view/window): menus + full-screen HUD. Component:
  **`UICanvasComponent`** with `renderMode = ScreenOverlay`, `document` (Ref<UIDocument>),
  `theme` (Ref<UITheme>, optional — falls back to project default), `order`, `visible`,
  `interactive`, scaler settings (mode + reference resolution, Flax `CanvasScaler` model).
  Living on an entity means HUDs ride in scenes and **prefabs** (a menu = a prefab with a
  canvas component — spawn/despawn to open/close; document alone can also be pushed from
  script for scene-less overlays).
- **World tier**: `renderMode = WorldSpace` on the same component + `worldSize`,
  `pixelsPerUnit`, `faceCamera`, `depthTest`. Rendered by **projected direct-draw** (Flax):
  the VG renderer draws the canvas's geometry with a full world×view×proj transform into the
  scene pass after transparents, optionally depth-tested. (Prereq: the VG renderer accepts
  an arbitrary 4×4 transform + target depth state — it renders with an ortho matrix today;
  this is a matrix swap plus pipeline-state variant, not a rearchitecture. If it fights
  back, fall back to Sedulous's RT+sprite mode behind the same component, and keep RT as an
  explicit `RenderTexture` mode later for UI-as-material.)
- **Billboard tier**: **`UIBillboardComponent`** — a small document anchored to the
  entity's projected screen position, with offset, distance scaling (fixed/distance,
  min/max), cylindrical/screen orientation. Port of the Sedulous billboard manager (its
  best-behaved piece) onto documents.

### 3.2 Rendering integration

- Screen tier renders in a **view overlay pass** after the 3D blit (one shared pass per
  view; canvases sorted by `order`). The player gains this pass; the editor's game page
  reuses it inside the viewport render.
- World tier renders inside the scene's frame graph after the transparent pass (before
  post, so TAA sees stable UI? — no: AFTER post, jitter-free overlay projection like Flax,
  see open question 3).
- Billboards batch into the view overlay pass with the screen tier (one VG batch).

### 3.3 Input routing

Strict phased contract per frame (Godot), implemented over the existing shell event stream:

```
raw events → [UI dispatch: screen canvases by order → billboards → world panels]
           → consumption mask (pointer/keyboard/text classes)
           → gameplay (action mapping evaluates with the mask applied; see input.md §3.3)
```

- First-consumer within UI (Sedulous chain semantics), **hard consumption** toward gameplay
  (Godot semantics) — no `IsMouseOverUI` polling contract; gameplay simply never sees
  consumed input through actions. (The raw device facades stay unfiltered for code that
  really wants them.)
- **World-panel picking**: ray from the active camera through the cursor (pluggable ray
  provider for split-screen) → OBB intersect per interactive world canvas → nearest wins →
  inverse world matrix → canvas-local point → normal UI hit test (Flax `Intersects3D`).
  Because it consumes the same event stream as everything else, it works under
  play-in-editor's `InputSurface` — fixing Sedulous's broken editor world-UI input by
  construction.
- **Focus & gamepad navigation**: one focus scope per canvas, navigation driven by input
  ACTIONS (`UI/Up|Down|Left|Right|Submit|Cancel` in a built-in action set) with hold-repeat;
  explicit per-view focus neighbors (new `View` properties) + the geometric fallback the
  framework already has. `WantsTextInput` (exists) gates text-vs-action keys.

## 4. Runtime resources

- **`UIDocument`** (cooked): a validated view-tree payload. v1 payload = the markup text
  (cook validates: parse + unknown-type/property errors fail the cook); v2 = pre-parsed
  binary tree (Traktor lesson — parsing out of the runtime; the loader seam stays the
  same). Factory instantiates a fresh view tree per canvas (documents are templates, not
  shared live trees). Hot reload: document rebuild on resource reload, state re-bound by
  view name where possible.
- **`UITheme`** (cooked): validated `.sss` payload → parsed StyleSheet at bind. Project
  settings gains `defaultThemeId`; per-canvas override ref. Ships the built-in code themes
  as fallback when nil. This is Godot's swappable-theme-asset model on our existing SSS.
- Both resolve through the standard `resource::Ref` + `ResolveSceneResources` pass;
  referenced images/fonts inside documents/themes resolve through the UI VFS provider
  against the cooked DB (dependency edges recorded at cook via `@import`/`image()` scans,
  so recooks cascade correctly).

## 5. Editor-side assets

- **`UIDocumentAsset`** / **`UIThemeAsset`** (source): text payloads. Created via New Asset
  ("UI Document", "UI Theme") with starter templates; also importable by dropping `.sml`/
  `.sss` files (trivial importer, no options dialog).
- **Builders**: validate + write-through (v1); document builder records image/font/import
  dependencies for the cook graph.
- **`UIDocumentPage`** (editor page): text editor pane + **live preview** pane rendering
  the document in a real UIContext with the project theme; save triggers recook + preview
  refresh; parse errors shown inline. (A visual WYSIWYG editor is explicitly out of scope —
  the preview page + hot reload is the honest v1.)
- **Inspectors**: canvas/billboard components via reflection (document/theme pickers filter
  by type — the AssetPickerDialog already supports type filters); world canvas gets a
  size/plane gizmo via the component-gizmo registry.
- Play-in-editor: canvases behave identically in Simulate/play (they're components); the
  screen tier renders into the game viewport's overlay.

## 6. Script/event wiring (Wren)

- v1: name-based lookup + events — `ui.canvas(entity).find("resume-btn").onClick { ... }`;
  document root exposed to the entity's script context. Named views are the `.sml` `id`
  attribute (exists).
- v2: declarative binding attribute in markup (`onClick="game.resume"`) resolving into the
  script context — Godot's designer-visible wiring without an editor dialog.

## 7. Phasing

- **P1 — subsystem + screen tier + assets**: `draconic.ui.subsystem` (host composition,
  overlay pass in player + game page), `UICanvasComponent` (ScreenOverlay only), UIDocument/
  UITheme assets→resources with validation cooks + hot reload, input consumption mask wired
  to the action layer, CanvasScaler-style scaling, sample: pause menu prefab + HUD in
  Sandbox/player. Tests: document/theme cook validation, canvas component serialization,
  input-consumption unit tests over synthetic streams.
- **P2 — billboards + nav + authoring**: `UIBillboardComponent` (batched, ported as-is
  from the reference), gamepad navigation (action set + focus neighbors), UIDocumentPage
  with live preview (EditText + side preview).
- **World tier — DEFERRED until designed right** (locked decision 8: no stopgap). Its own
  phase, entered only after the design conversation (projected direct-draw needs the VG
  consultation; the alternative is a batched RT/atlas). Ray picking + `worldSize`/
  `depthTest` component fields ride with it.
- **P3 — polish**: RenderTexture canvas mode (UI as material input), declarative script
  bindings (still blocked on Wren entity handles), theme variations.

## 8. Open questions

All four original questions are resolved (see §0): 1 = follow Sedulous's tier shape
(refined after the reference read); 2 & 4 = verified in code; 3 = after post via a
registered overlay stage.

## 9. Backlog (post-P2, reviewed with the user 2026-07-18)

**P3 CORE SHIPPED (agent-built, merged 3c35f15):** order-stacking (CanvasHostView per
canvas, MoveView pure reorder - focus survives; despawn/component-removal SWEEP - the
user-reported "removed component, UI still renders" bug; billboards got the same sweep
in the merge), ReferenceResolution scaler (letterboxed min-fit via child ViewTransform,
hit-test follows), keyboard/text input (IInputSourceProvider::Events() seam, UiInputBridge
on the game context, IME via SetTextInputTarget in player / WantsTextInput forwarding in
the Game tab), RenderTexture canvas mode (standalone RootView + subsystem-owned targets,
RenderCanvasTextures host seam before BeginRendering; sprite/decal runtime texture
override consumes it today), project-default UITheme (manifest v5 defaultUiThemeId,
shares v5 with audio's defaultBusLayoutId) + GameLightTheme. BONUS FIX: EditorProject::
Open dropped defaultInputMapId (nil map every open). Remaining below stays live.

What remains now that P1 + P2 + the overlay-roles refactor + split-screen are shipped.
Suggested order: scaler + order-stacking (small, closes the P1 deviations) -> keyboard/
text input (completes the input story) -> RenderTexture mode -> the world-tier design
conversation. Script bindings stay parked until Wren entity handles land.

### P3 — fidelity + authoring
- **Canvas order-stacking**: `UICanvasComponent.order` serializes + shows in the
  inspector but is NOT applied - canvases stack in component order. Sort each scene
  root's canvas children by `order` (billboard layer stays below).
- **Canvas scaler**: `scalerMode`/`referenceResolution` serialize but layout always runs
  ConstantPixel. ReferenceResolution = uniform-scale the canvas root (Transform.Scale)
  so the reference resolution fits the viewport; hit-testing follows automatically
  (View transform hit-tests). Interacts with billboards NOT scaling (their own modes).
- **Keyboard/text input into the game tier**: the consumption mask already reserves the
  keyboard when a game EditText holds focus, but no key/text events reach the game
  context - the tier is pointer + gamepad only. Feed key/text through the SAME
  InputSubsystem facades the pump uses (player window + Game-tab InputSurface work
  transparently), incl. WantsTextInput -> SDL text-input start/stop via the shell.
- **RenderTexture canvas mode**: a canvas rendering into an offscreen texture instead
  of the overlay pass (in-world screens, scoreboards; UI as material input). Reuses the
  preview seam's draw path (DrawRootInto against a caller target). Also the stepping
  stone for the world tier's batched-RT option.
- **Declarative script bindings** (PARKED - blocked on Wren entity handles, same
  blocker as physics/audio per-entity APIs): onClick handlers in markup + canvas/view
  lookups from game scripts.
- **Theme variations**: cooked UITheme assets + per-canvas overrides work; remaining =
  a project-settings default theme id + built-in variants beyond GameTheme.

### World tier — DECIDED with the user 2026-07-19: B now, A as a later per-panel mode
Decisions: (1) RT-quad panels ship; projected direct-draw becomes a future per-panel
MODE on the same component (its VG consult stays open until then). (2) Resolution =
PIXELS-PER-METER (uniform density; default ~200 px/m). (3) INTERACTIVE from day one
(camera ray -> quad -> UV -> pointer injection, gated by the per-surface scene
binding). (4) UNLIT/emissive only.

Shape: `ui.WorldPanel` component {document, theme, sizeMeters (Float2), pixelsPerMeter,
interactive, visible}. The panel reuses the RT-canvas target machinery (per-panel
offscreen target keyed (scene, entity, kind); RenderCanvasTextures renders it) and
DRIVES a sibling SpriteComponent (auto-managed): orientation = the NEW
SpriteOrientation::EntityOriented (quad spanned by the entity's world right/up axes -
the one render-side addition), size = sizeMeters, runtime texture override = the panel
target (the shipped auto-bind lifecycle: rebind on resize, unbind before free). Being
ordinary scene content, panels get depth/occlusion/TAA/post correct BY CONSTRUCTION -
the exact property option A struggles with. Not the Sedulous wart because: dirty-
gating (follow-up), the safe auto-bind lifecycle, and REAL input routing.

Input: when the pointer misses the overlay tiers, the pump ray-casts the scene's
primary camera through the pointer (scene-root ViewportSize = the surface's content
size), intersects interactive panels' planes (entity plane, half-extent test), takes
the nearest hit, converts UV -> panel pixels, and routes the panel's standalone root
through the SAME active-input-root + scene-binding rules as every tier. Ray/UV math is
a pure helper (unit-tested).

**SHIPPED acf506e** (same day as the decision): EntityOriented sprites (per-instance
entity right/up axes - the Sedulous reference only had fixed world-XY WorldAligned),
ui.WorldPanel + sprite auto-drive + ppm-sized RT targets + ray-interactive input under
the scene-binding rules; kiosk demo in PhysicsPlayground; tests incl. the full headless
loop on the Null RHI.
Follow-ups: dirty-gated redraws (skip unchanged canvases), atlas packing for many
small panels, panel-target MIP chains (distant minification shimmers - found during
the AA review; content AA = VG analytic, edge AA = the pass sits between tonemap and
FXAA), the A-mode (direct-draw crispness) when a game needs it.

### Known edges (fix when the workflow becomes real)
- **Two interactive scenes visible at once**: pointer routing probes scene roots in
  creation order - overlapping UI coordinates can hit the wrong scene. Proper fix:
  bind input routing to the scene owning the ACTIVE InputSurface (per-surface scene
  binding), not a global probe order.
- **UIDocumentPage** eventually wants a real code-editor control (line numbers,
  highlighting) instead of the multiline EditText (honest-v1 per locked decision 7).

### Adjacent (NOT game-ui; tracked here so it isn't lost)
- Toolkit tail (editor-side): port the ~212 verbatim upstream toolkit tests + the
  remaining UISandbox tabs (Docking last).
