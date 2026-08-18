# Navigation P4b (editor UI): how to consume the viewport-tool seams

Status: DESIGN QUESTION for Fable (Opus, 2026-08-18). The navigation runtime + bake
core are built and tested (navigation.md P0-P4a). What remains is the editor UI:
zone gizmo, a Bake action, and navmesh/agent-path debug draw. This is exactly the
consumer the viewport-tool seams were built for (property-animation.md Phase H said
so), so before building I want a ruling on how navigation should plug in.

## What already exists (verified 2026-08-18)

The in-scene authoring seams from the property-animation work, all navigation-ready:

- **`editor.viewporttools` / `IViewportTool`** (Editor.ViewportTools/ViewportTools.cppm:82)
  - a UI-free modal interaction mode: `Id()`, `DisplayName()`, `OnActivate/OnDeactivate`,
  `Update(input)`, `Draw(DebugDraw&)` (built-in debug-draw hook), `StatusText()`.
  Registered via `IViewportToolProvider` -> `ViewportToolProviderRegistry`.
- **`editor.app:tool_panel` / `IViewportToolPanelProvider`** (Editor.App/ToolPanel.cppm)
  - a UI settings panel keyed by tool id, ACTIVATION-scoped, mounted beside the viewport
  while its tool is active. The seam's own doc names the consumers: "property-animation
  authoring, a future terrain-brush panel, **a nav-mesh bake panel**." Registered via
  `ViewportToolPanelRegistry`.
- **`GizmoRendererRegistry` / `IGizmoRenderer`** (Editor.Scene/ComponentGizmos.cppm:41)
  - a per-component gizmo keyed by `ComponentType()`, with `DrawWhenUnselected()`. Box-volume
  precedents already exist: `DecalGizmoRenderer`, `ReflectionProbeGizmoRenderer`.
- **Per-domain registrar precedent**: `RegisterPropertyAnimationEditor(EditorContext&,
  IApplicationHost&)` (Editor.PropertyAnimation/PropertyAnimationEditorModule.cppm:75) is
  where a domain registers its tool provider + panel provider + gizmos at editor startup.

## How P4b maps onto them (proposed)

- **Zone gizmo**: an `IGizmoRenderer` for `NavMeshZoneComponent` drawing the extents box
  (mirror `DecalGizmoRenderer`), registered in `GizmoRendererRegistry`.
- **Bake trigger + params**: a `NavigationTool : IViewportTool` (the mode) + a
  `NavigationToolPanelProvider : IViewportToolPanelProvider` whose panel shows the bake
  profile fields + a "Bake" button that calls the existing `BakeNavigationZone`.
- **Debug draw** (navmesh polys + agent paths): the tool's `Draw(DebugDraw&)` and/or a
  persistent per-scene debug-draw contributor.
- **`RegisterNavigationEditor(context, host)`**: the registrar, wired into the editor app
  beside `RegisterPropertyAnimationEditor`.

## Open decisions (what I need from Fable)

1. **Tool-mode vs one-shot action.** The seam is built around a persistent MODE (terrain
   brush, property-animation authoring). Baking a zone is arguably a ONE-SHOT action on the
   selected zone, not a mode you dwell in. Three shapes:
   - (a) Full `NavigationTool` mode; its panel lists the scene's zones with a per-zone Bake
     button + debug-draw toggles. Baking is done "inside navigation mode."
   - (b) No tool: Bake is an inspector/component-context button on `NavMeshZoneComponent`,
     and debug draw is a persistent view toggle. The tool_panel seam goes unused by nav.
   - (c) Hybrid: a lightweight "Navigation" mode that exists mainly to host the panel + turn
     on debug draw; the Bake button lives in the panel.
   Which matches the intent when we said "nav-mesh bake panel"? My lean: (c) - it uses the
   seam as designed and keeps the bake discoverable, without pretending baking is a dwell-mode.

2. **UI-capable editor lib layering.** `Editor.Navigation` is currently UI-free (the bake
   core, linking engine.render/geometry). The tool/panel/gizmo build `ui::View`s and link
   editor.viewporttools + editor.app + editor.scene(gizmo). Add them to `Editor.Navigation`
   (make it UI-capable), or split a UI-free core from an `Editor.Navigation.UI`? The
   pipeline-UI-free rule is about Pipeline targets, not editor libs - is there an editor-tier
   convention here, or does property-animation's single UI-capable editor lib set the pattern?

3. **Debug-draw scope.** Tool `Draw()` is activation-scoped (only while nav mode is active).
   Navmesh + path visualization usually wants to PERSIST regardless of the active tool (like
   collider debug draw). Should nav debug draw be tool-scoped `Draw()`, or a persistent
   per-scene debug-draw contributor with its own view toggle? (Leaning persistent + toggle.)

4. **Async bake.** `BakeNavigationZone` is synchronous today. The spec says the editor bake
   runs async on a worker with progress. For small zones it is instant. Build the job-system
   + progress wrapper now, or defer async until a real large-zone bake actually stutters?
   (Leaning defer - keep P4b synchronous, add async when measured.)

5. **Gizmo interactivity.** Read-only extents box (like Decal), or interactive resize handles
   (like `TransformGizmo`)? Read-only is far cheaper and the extents are also inspector-editable.
   (Leaning read-only for P4b.)

6. **Navmesh debug geometry accessor.** The runtime `NavigationMesh` has no
   triangle-extraction accessor. For navmesh debug draw I would add
   `NavigationMesh::DebugTriangles(Array<Float3>&)` (walk the dt tile polys) in
   foundation.navigation. Confirm that is the right layer (vs caching a debug outline in the
   cooked `NavigationZoneSource` at bake time, the collision-asset precedent).

## Not blocked

None of this blocks the runtime - agents already navigate (P3). This is purely the authoring
surface, and every piece has a clear seam to land on; I only need the shape decisions above
before building so I do not build the wrong shell.
