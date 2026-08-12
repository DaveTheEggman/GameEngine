# Game UI - deferred follow-ups

> Status: CURRENT
> Track: [[game-ui-subsystem]]

What remains after P1 + P2 + the overlay-roles refactor + split-screen + the world tier shipped
(`Documentation/Systems/game-ui.md`). Each item is scoped; none blocks the shipped surface.

## World tier

- **Option-A direct-draw mode.** The RT-quad world panel (option B) shipped; projected direct-draw
  (widget tree drawn with a wvp transform for crispness at distance) becomes a future per-panel MODE on
  the same component. VG-CONSULT-GATED (locked decision 9) - do not touch the VG renderer without the
  conversation.
- **Dirty-gated redraws** - skip re-rendering unchanged world-panel canvases (the Sedulous
  MarkDirty/PostUpdate pattern).
- **Atlas packing** for many small panels + **panel-target MIP chains** - distant minification shimmers
  (found in the AA review; content AA = VG analytic, edge AA = the pass between tonemap and FXAA).

## Authoring + scripting

- **Declarative markup bindings** - `onClick="game.resume"` resolved into the script context (Godot's
  designer-visible wiring without a dialog), beyond today's imperative `onClick` delegate.
- **Theme variations** - cooked UITheme assets + per-canvas overrides work; remaining = built-in
  variants beyond `GameTheme`/`GameLightTheme`.
- **UIDocumentPage code-editor control** - the page eventually wants a real code editor (line numbers,
  highlighting) instead of the multi-line EditText (honest-v1 per locked decision 7).

## Known edges (fix when the workflow becomes real)

- **Two interactive scenes visible at once** - pointer routing probes scene roots in creation order;
  overlapping UI coordinates can hit the wrong scene. The per-surface scene binding shipped and is the
  intended fix path; harden it when a real two-scene workflow appears.

## Adjacent (NOT game-ui; tracked so it is not lost)

- **Toolkit tail** (editor-side) - port the ~212 verbatim upstream toolkit tests + the remaining
  UISandbox tabs (Docking last).
