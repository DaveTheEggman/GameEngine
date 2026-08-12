# Game UI

> Status: CURRENT
> Verified: 2026-08-12 @ 3364be29
> Track: [[game-ui-subsystem]] / [[game-ui-p1-progress]]

The game-facing UI runtime: `foundation.ui`'s retained-mode control framework driven as an engine
subsystem, showing UI three ways (screen HUD/menus, world-anchored billboards, world-space panels) as
cooked document + theme assets, with actions-only input arbitration and a script facade. Shipped end
to end (P1-P3 + the overlay-roles refactor + split-screen + the world tier).

## Modules

- **Framework** (`foundation.ui` + `.toolkit` / `.runtime` / `.viewport` / `.shell` / `.vfs` /
  `.application`) - the retained-mode UI stack (`.sml` markup + `.sss` styling loaders, VG renderer,
  themes). Shared with the editor's UIHost and UISandbox: ONE host implementation.
- **`foundation.ui.resource`** (`Code/Foundation/UI.Resource`) - the cooked `UIDocument` + `UITheme`
  resources + factories.
- **`ui.pipeline`** (`Code/Pipeline/UI.Pipeline`) - `UIDocumentAsset` / `UIThemeAsset` + builders (cook
  = validate markup/styling + write-through, recording image/font dependency edges).
- **`engine.ui`** (`Code/Engine/Engine.UI`) - `UISubsystem`, the canvas / billboard / world-panel
  components, the overlay-role bridges, the consumption mask, `GameTheme`, and the UI script facade.
- **`editor.gameui`** (`Code/Editor/Editor.GameUI`) - `UIDocumentPage` (text pane + live preview
  rendered through the runtime context).

## Tiers + components

All rendered through the SAME `UIContext`/root/VG machinery, no parallel host:
- **Screen** - `UICanvasComponent` (`CanvasRenderMode::ScreenOverlay`): menus + HUD, with a document +
  optional theme ref, `order` stacking, and a `CanvasScaler` (ConstantPixel / ReferenceResolution,
  letterboxed min-fit). A menu is a prefab (spawn/despawn = open/close); documents can also be pushed
  scene-less onto the screen tier (`PushScreenOverlay`, topmost, survives scene swaps).
- **Billboards** - `UIBillboardComponent`: a small document anchored to the entity's projected screen
  position (offset + distance scaling), batched into one VG draw (ported from the Sedulous reference,
  its best-behaved piece).
- **World panels** - `UIWorldPanelComponent`: an RT-quad panel (per-panel offscreen target keyed
  (scene, entity, kind)) driving an auto-managed sibling `SpriteComponent` with
  `SpriteOrientation::EntityOriented` (quad on the entity's right/up axes), sized in pixels-per-meter,
  unlit, INTERACTIVE (camera-ray -> plane -> UV -> pointer injection). Being ordinary scene content it
  gets depth / occlusion / TAA / post correct by construction.

## Overlay roles + rendering

The render layer coordinates overlays two-tier (the a78ec94 refactor): **`ISceneOverlay`** (per-view,
drawn inside the compose after post / before debug draw, matched by SceneKey, given the view's real
camera) + **`IScreenOverlay`/`IScreenRenderer`** (window-space registry; hosts make one generic
RenderOverlays call per target). `UISubsystem` implements both: per-scene roots (billboards + canvases;
scene isolation is structural; billboards project per view - correct in editor viewports/camera
previews) plus the scene-less screen root. Split-screen: the VG renderer has a sub-rect Render overload
(viewport offset + scissors clamped to the rect), so scene HUDs lay out per half. The VG ring resets
once per UI frame. A `RenderTexture` canvas mode renders a canvas into an offscreen target (in-world
screens; UI as a sprite/decal texture override).

## Input

Actions-only arbitration (no `IsMouseOverUI` polling). UI dispatches first (screen canvases by order ->
billboards -> world panels), then `ActionRuntime::SetConsumptionMask` mutes the matched input CLASSES
(pointer / keyboard / text - SEPARATE, so a menu eating the mouse never mutes gamepad movement) for
gameplay that frame; raw device facades stay unfiltered. Per-surface scene binding
(`SetSourceProvider(provider, sceneKey)` + `UnboundInputScenePolicy`) resolves the active input root
(occupied screen tier = modal > pointer-hit root > first scene root with content for pad-only nav);
GamePage binds on Play, the editor's embedded context runs ScreenTierOnly (HUDs are WYSIWYG but not
interactive while editing). Gamepad navigation (dpad/stick MoveFocus + hold-repeat, South = activate,
East = escape; pad is NOT a consumption class) and keyboard/text input (IME via the shell) are wired.
Play-in-editor works by construction (the same event stream through the viewport `InputSurface`).

## Resources + script

- **`UIDocument`** (cooked) - a validated view-tree payload (markup text v1; the cook fails on
  unknown types/properties); the factory instantiates a fresh tree per canvas (documents are
  templates). Hot reload rebuilds on resource reload.
- **`UITheme`** (cooked) - a validated `.sss` payload; a project `defaultUiThemeId` (manifest v5,
  shared with audio's bus layout) selects it, per-canvas override allowed, `GameTheme`/`GameLightTheme`
  as built-in fallbacks. Swappable-theme-asset model (Godot) over our SSS.
- **UI facade** - addresses a control by its authored `id`: `setText` / `setProgress` / `setVisible`,
  and `onClick` binds a script delegate, resolved through the live screen tier + resource manager.

## Locked decisions

Core UI only (never the toolkit - that is editor tooling); one `UIContext` owned by the subsystem with
the theme on the context; actions-only input gating with separate consumption classes; the VG renderer
is OFF-LIMITS without a consult (game-UI needs no VG changes). UI updates with UNSCALED dt (menus
animate while the game is paused).

## Deferred

World-tier direct-draw mode (option A, VG-consult-gated), dirty-gated panel redraws, atlas packing +
panel MIP chains, declarative markup bindings (`onClick="game.resume"`), theme variations beyond the
built-ins, the UIDocumentPage code-editor control, the two-interactive-scenes routing edge, and the
toolkit test tail: `Documentation/Backlog/game-ui-followups.md`.

---

Design rationale (the reference survey - Sedulous / Flax / Godot / Traktor Spark - the Sedulous.Engine.UI
deep-read tier table, the locked-decision reasoning, the world-tier A-vs-B decision) is in
`Documentation/Archive/game-ui-design-history.md`.
