# Shared editor preview viewport (extract the triplicated bespoke-page 3D preview)

Status: DESIGN QUESTION for Fable (Opus, 2026-08-18). Origin: the bespoke-pages UAT
cluster (week-2026-08-15) - specifically "Collision mesh page: no preview". Building
that preview surfaced that the 3D-preview scaffolding is already triplicated and that
Editor.Physics cannot reuse it. The user notes Sedulous landed on a shared preview
viewport and considers that the right call; this poses the extraction for a ruling.

## The finding (verified 2026-08-18)

Three bespoke pages in Editor.Scene each HAND-ROLL the identical 3D-preview
scaffolding - same members, same methods, same render loop:

- `MeshPage`, `AnimationClipPage`, `SkeletonPage` each declare:
  `RefPtr<ui::viewport::ViewportView> m_viewport`, `scene::SceneManager m_sceneManager`
  (its own preview scene group), `engine::render::RenderSubsystem* m_render`,
  `EditorCamera m_camera`, `UniquePtr<shell::InputRouter> m_router`,
  `graphics::RenderWindow* m_hostWindow`, and `void EnsureViewportBound()`.
- Each `OnUpdate` runs the same loop: EnsureViewportBound -> SyncInputRegion ->
  m_router->Update -> (hovered/focused ? m_camera.Update) -> build a `ViewCamera`
  from the camera -> `m_render->RenderScene(scene, viewport target, format, w, h,
  cameraOverride)`, with debug overlays drawn into `DebugScene(scene)`.
- `EditorCamera` (EditorCamera.cppm, ~194 lines, foundation-only: imports just
  foundation.core + foundation.shell) is a PRIVATE `editor.scene` partition
  (`import :camera`). Nothing outside editor.scene can use it.

The consequence that triggered this: the `CollisionShapeEditorPage` lives in
Editor.Physics, which links NONE of the preview stack, and its factory is not even
handed host/uiHost. To give it a preview I must either copy the ~150 lines a FOURTH
time (and add a local camera, since EditorCamera is unreachable) or extract the shared
piece. The user (and the Sedulous precedent) favor extraction.

## Proposal

Extract a shared **PreviewViewport** helper that owns the whole 3D-preview substrate,
so a bespoke page CONTAINS one instead of re-implementing it:

- Owns: the `ViewportView`, a per-page `SceneManager` + preview scene, the
  `EditorCamera`, the `InputRouter`, the host-window binding, and the
  EnsureViewportBound + per-frame camera-update + `RenderScene` loop.
- A page: constructs a PreviewViewport (from host + render/scene subsystems), adds its
  own entities to `preview.Scene()` (mesh, light, ...), calls `preview.Update(dt)` each
  frame, and mounts `preview.View()`. An optional draw hook (or the page drawing into
  `render->DebugScene(preview.Scene())` itself) covers overlays - the skeleton wireframe,
  the collision outline, the navmesh, etc.
- `EditorCamera` graduates out of the editor.scene partition into the shared module so
  it stops being editor.scene-private (it is already foundation-only).

This kills the triplication, unblocks Editor.Physics (and any future bespoke page), and
is the shape the user reports Sedulous converged on.

## Open questions for Fable

1. **Where does the shared module live?** editor.core is the headless editor domain (no
   render/shell) - it likely CANNOT host a render+shell-dependent viewport helper without
   dragging heavy deps into a deliberately-light lib. A NEW small module (editor.preview /
   editor.viewport-preview) that both Editor.Scene and Editor.Physics link seems right.
   Confirm the home + that it does not violate the editor-core-stays-light intent.
2. **Draw-overlay seam.** Should PreviewViewport expose a `Function<void(DebugDraw&)>`
   draw hook, or just hand out `Scene()` + let the page draw into `DebugScene` itself (as
   the pages do today)? The latter is less API; the former is more encapsulated.
3. **Migration order.** Extract + adopt in the NEW consumer (the collision page) first -
   zero risk to working pages - then migrate mesh/clip/skeleton incrementally? Or migrate
   all three in the extraction commit (bigger, proves the API against all real consumers at
   once, per the "design against real consumers" rule)?
4. **Factory threading.** The collision page's factory + RegisterCollisionShapeEditor must
   start taking host/uiHost (they do not today) to reach the subsystems. In-scope for this
   work, or a prerequisite commit?
5. **Naming** of the helper + the module.

## Tentative recommendation (Opus, for Fable to challenge)

- Extract into a new lean module both editor libs link; promote EditorCamera into it.
- Hand out `Scene()` + `Update(dt)` + `View()`; let pages draw overlays into `DebugScene`
  as they already do (question 2 = the low-API option) unless Fable wants the hook.
- Adopt in the collision page first (new consumer, no regression risk), THEN migrate the
  three existing pages in a follow-up - but design the API against all four up front so it
  is not shaped by one consumer (the seam-rot lesson from navigation-editor-ui.md).

Not blocked otherwise: the rest of the bespoke-pages cluster (the graph-page param editors,
the ports bug) does not depend on this; only the collision preview does.

## Not to be confused with

- `Documentation/Specs/camera-preview.md` (task #118, BUILT) - the floating in-viewport
  live preview of a SELECTED CameraComponent's view. Different feature; this is about the
  bespoke-page authoring viewport substrate.
- The week-2026-08-22 seeded "editor extensibility seams" item (inspector action-row +
  gizmo-renderer registries). Orthogonal: that makes per-DOMAIN surfaces plugin-extensible;
  this deduplicates the per-PAGE 3D-preview scaffolding. They could land in either order.
- Today only `DrawSkeletonWireframe` (a free draw helper in :animation_graph_page) is
  shared across pages - the viewport + scene + camera + render loop is NOT.
