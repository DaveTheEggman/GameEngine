# Scene editor: floating, pinnable camera preview (task #118)

> STATUS: BUILT 2026-08-03 (cd8201b9 core, 07daee85 overlay, 2e4db157
> view-keyed debug draw so gizmos stay out of the preview) + tests
> (CameraPreviewTests.cpp). The preview appears when an entity WITH a
> CameraComponent is selected (none selected = no preview - by design).
> Remaining: on-screen user verify only (UAT editor-misc session).
> Header added 2026-08-15 - the spec was never stamped at build time,
> which left task #118 looking open.

Size: M. Modules: `Code/Draconic/Editor/Draconic.Editor.Scene/`
(ScenePageImpl.cpp + a new preview view class),
`Code/Draconic/Foundation/Draconic.UI.Viewport/` (reuse, ideally unchanged).

## Context

Godot/Unity behavior: selecting an entity that has a CameraComponent shows a
small live preview of THAT camera's view, floating in a corner of the scene
viewport, with a pin toggle to keep it visible after deselection.

The machinery already exists:

- The scene page renders through the real renderer with a
  `render::CameraOverride` into a `ui::viewport::ViewportView`
  (ScenePageImpl.cpp ~line 165-185: builds CameraOverride, calls the render
  subsystem with a ViewportRect + TargetState). The preview is the SAME call
  with a camera override built from the selected entity's CameraComponent
  world transform + lens fields instead of the editor camera.
- Multi-view per frame is supported and correct (one graph for all views;
  per-view cluster buffers already fixed - see memory
  `multi-view-engine-survey`). A second small view per frame is the designed
  use case.

## Design

1. `CameraPreviewView`: a small overlay view (child of the scene page's
   viewport container, anchored bottom-right with a margin) hosting its own
   ViewportView at a fixed aspect-correct size (e.g. 320 wide, height from the
   camera's aspect; clamp to a fraction of the viewport). It renders ONLY
   while visible.
2. Visibility: shown when the current selection has a CameraComponent, hidden
   on deselect - unless pinned. Pin = a small pushpin toggle button in the
   preview's corner (use an existing theme icon; editor font only rasterizes
   codepoints <= 255, so use a drawn/atlas icon, NOT an out-of-range glyph -
   memory `editor-font-glyph-range`).
3. Pinned state remembers the ENTITY (EntityHandle), not "whatever is
   selected": pin, select something else, the preview keeps showing the pinned
   camera. Unpin hides it if the selection is not a camera. If the pinned
   entity dies (despawn/scene reload), unpin gracefully.
4. Render: per frame, when visible, build a CameraOverride from the target
   camera component (transform, fov/near/far, clear color) and drive the
   preview ViewportView exactly as the main viewport does (same TargetState
   pattern, same resize handling - ViewportView resize destroys/recreates
   targets; follow the main viewport's precedent in ScenePageImpl).
5. The preview gets NO input surface: it is display-only (do not register an
   InputSurface; clicks fall through to normal UI hit-testing on the preview
   view itself, which should consume them so the 3D viewport underneath does
   not orbit when the user grabs the pin).
6. Float/drag is NOT in scope (fixed corner). "Floating" in the task title
   means floating OVER the viewport, not an OS window. Keep the door open by
   making the anchor a layout property.
7. Per-page: each open scene page owns its preview state independently. Pin
   state is editor-session state - do NOT persist it into any runtime/wire
   struct (memory rule `no-editor-data-in-runtime`); persisting per-scene pin
   in editor settings is optional and NOT required.

## Gotchas

- Simulate/play tabs: the preview must keep working when the page enters
  Simulate (same scene pointer changes semantics; test a pin across
  Simulate start/stop - the snapshot/restore may invalidate the entity).
  If the pinned handle goes stale, unpin.
- Do not render the preview when its size would be 0 or the page is hidden
  (docked-inactive) - follow the main viewport's guard.
- The overlay must not trigger the UI mutation rule: creating/destroying the
  preview view in response to selection events must go through
  `MutationQueueRef().QueueAction` if done from event dispatch.

## Tests (Editor.Scene tests target)

- Selection logic unit tests: camera-selected -> preview visible;
  deselect -> hidden; pin -> stays; pinned entity destroyed -> auto-unpin.
  (Factor the visibility/pin decision into a testable non-UI helper.)
- CameraOverride construction from a CameraComponent (fov/near/far/transform
  mapping) as a pure function test.
- Existing scene-page tests stay green.

## Acceptance

- User-verifiable: select camera in hierarchy -> live preview bottom-right;
  pin it; select a mesh; preview persists; unpin; preview hides; Simulate
  start/stop does not crash or leak targets (WaitIdle discipline on resize
  follows the existing precedent).
- Both compilers green; no new validation errors on Vulkan.

---

## State (appended 2026-08-03; original content above is unchanged)

**SHIPPED.** P1 core / CameraOverride + pin/visibility (cd8201b9), on-screen
overlay phase 2 (07daee85), and view-keyed debug draw so editor gizmos stay out of
the preview (2e4db157). Green clang+gcc.
