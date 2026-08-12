# Code Editor - design history (archived)

> Status: ARCHIVED
> Superseded by: Documentation/Systems/code-editor.md
> Track: [[code-editor-track]]

NON-AUTHORITATIVE. The design decisions + phase history behind the 2026-07 CodeEditView track.
Present-tense truth is `Systems/code-editor.md`; the full original doc is in git at the P0 commit
3b92560d. Kept for the "why".

## What it replaced

`ScriptPage` was a multi-line `EditText` + an external `BreakpointGutter` reading its line metrics;
`UIDocumentPage` was the same `EditText` for XML. One flat `String`, full reshape per edit, single-style
draw, no marker model. The track deleted `BreakpointGutter` (breakpoints became native markers).

## Key decisions

- **Purpose-built widget** (`ui::toolkit::CodeEditView`), NOT a vendored editor (Scintilla et al. assume
  a foreign rendering/input stack; the port cost approx the build cost plus a permanent
  no-STL/-fno-exceptions tax) and NOT more bolt-ons over `EditText` (a form control with a flat-string
  model that blocks highlighting, markers, and large-file performance - and that game UI also uses).
- **Lives in `ui.toolkit`** beside CurveCanvas/NodeGraphCanvas, themed via the palette.
- **Completion in scope from the START** - the popup + provider seam shaped the widget's input routing
  from day one (keys route through the open popup first), not a later add-on.
- **Lexer layering** (decided with the user): the toolkit ships lexer MACHINERY + an id-keyed
  `CodeLexerRegistry` but ZERO language tables (same shape as `ICompletionProvider`). Language facts live
  in per-language editor-UI targets, siblings of the cook targets, because those link into the
  cook/export path which must stay widget-layer-free; only the editor app links the UI targets.
- File layout: everything under `UI/Toolkit/Code/` - a slim `CodeLexer.cppm` interface + per-concern
  implementation units, with shared scanning primitives in an INTERNAL partition (`:code_lexer_scan` -
  a partition implementation unit; GCC rejects non-exported interface partitions).
- Sequenced first of three queued conversations (code editor -> shaders -> Emscripten/WebGPU), because
  scripting needed it and the shader page will need it.

## Phase history (all shipped, user-verified on screen)

- **P1 - core + shell integration**: buffer, virtualized monospace render, cursor/selection/clipboard/
  undo, gutter + markers, popup infrastructure + document-word provider; ScriptPage + UIDocumentPage
  migrated; the `View::WantsTabKey` core input fix (Tab dispatch-first); bundled DejaVu Sans Mono as
  the "Mono" family. First-run fixes: caret auto-scroll vs the ScrollBar stale-max clamp; gutter clip
  against horizontally scrolled text.
- **P2 - syntax highlighting**: the `ICodeLexer` line-state seam + incremental `CodeHighlighter`
  (relex-until-convergence), `CLikeLexer` + `XmlLexer`, themed `CodeTokenColors`.
- **P3 - diagnostics + ergonomics**: find/replace bar (case/whole-word, live highlights, F3),
  go-to-line (Ctrl+G), comment toggle (`ICodeLexer::LineCommentPrefix`; XML opts out), bracket-pair
  highlight, brace-aware Enter indent, diagnostic hover tooltips, UI-document XML parse errors as
  line-anchored markers.
- **P4 - rich completion + debugger seam**: `ScriptApiCompletionProvider` (backend-neutral over
  `DescribeBoundApi`), `MarkupCompletionProvider`, ExecutionLine plumbing end-to-end
  (`EditorContext::ScriptExecutionPoint`), the hover-value seam (`ScriptValueProbe`). Second smoke pass
  fixed five findings (two-row find bar, punctuation key bridge, non-focus-stealing tooltips, completion
  ranking tiers + overflow indicator, ToggleButtons).
- ExecutionLine + hover-values RESOLVED 2026-07-29: the AngelScript debugger was already live
  (suspension-based, battery-certified); the earlier "backends return null" belief came from a stale
  interface comment.
