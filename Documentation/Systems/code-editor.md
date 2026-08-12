# Code Editor

> Status: CURRENT
> Verified: 2026-08-12 @ b97b3952
> Track: [[code-editor-track]]

`CodeEditView` - a purpose-built code-editing widget in `foundation.ui.toolkit`, VG-drawn and themed
like every toolkit control. Shipped end to end (P1-P4) and user-verified on screen. It backs the script
editor and the UI-document editor, with syntax highlighting, diagnostics, find/replace, a debugger
seam, and reflection-fed completion. Language tables live OUTSIDE the toolkit (per-language editor
targets); the toolkit ships only machinery.

## The widget (`ui::toolkit::CodeEditView`)

- **Buffer** (`CodeDocument`): an array of lines (source files are KBs; a line array is cache-friendly,
  virtualizable, and maps 1:1 to lexer/marker state). Cursor/selection are (line, column). It carries
  `FindAll`, compound edits (one-undo `ReplaceAll`), and `FindMatchingBracket`.
- **Rendering**: virtualized - only visible lines are shaped/drawn; the monospace fast path
  (column = x / advance) makes hit-testing, cursors, and column selection cheap. Token styles resolve
  through the theme palette (`CodeTokenColors`).
- **Editing core**: cursor/selection/clipboard, word motion, line ops, brace-aware Enter indent,
  bracket matching, comment toggle (via the lexer's `LineCommentPrefix`, Ctrl+/), and a coalescing
  undo stack of edit deltas.
- **Gutter + markers**: first-class per-line marker flags (Breakpoint, Error, Warning, ExecutionLine,
  Modified) + a clickable line-number margin. Breakpoints are native markers over the shared
  `EditorContext` store; compile diagnostics inject Error/Warning markers with tooltips.

## Lexing + languages

`ICodeLexer` is a line-state seam (a lexer gets one line + the ENTRY state, returns styled spans + the
EXIT state); `CodeHighlighter` relexes from the edited line downward until exit states converge (lazy
frontier). The toolkit ships lexer MACHINERY + an id-keyed `CodeLexerRegistry` but ZERO language tables
(the same shape as `ICompletionProvider`). Shipped lexers: `CLikeLexer` (configurable - the C-like
languages, Wren + AngelScript), `XmlLexer` (stateful; XML is generic, so the UI-document page
constructs it directly), and `LuaLikeLexer` (Luau).

Language facts live in per-language editor-UI targets - `editor.script.wren`, `editor.script.angelscript`,
`editor.script.luau` - siblings of the cook targets, because the cook/export path must stay
widget-layer-free; only the editor app links the UI targets and registers them. (HLSL's lexer spec is
deferred to the shader page - see backlog.)

## Completion

- **`ICompletionProvider`**: `(buffer, line, column, triggerKind) -> candidates`, per-language and
  COMPOSABLE. A document-word provider (buffer identifiers) backs every language; richer providers layer
  on top.
- **`ScriptApiCompletionProvider`** (`editor.script`) is backend-NEUTRAL over
  `IScriptManager::DescribeBoundApi` (a throwaway manager per language replays the runtime's exact
  registration once, lazily; top-level type/namespace names, a type's members after `Type.`).
- **`MarkupCompletionProvider`** (toolkit) over the `MarkupRegistry` element/attribute enumeration (the
  `<`-triggered UI-document vocabulary).
- Popup: anchored at the cursor, filtered as typed, arrows/Tab/Enter/Escape routed to it while open,
  never steals focus; ranking tiers + an overflow indicator.

## Diagnostics + debugger seam

Find/replace (in-widget bar, case/whole-word, live highlights, F3), go-to-line (Ctrl+G), diagnostic
hover tooltips, and UI-document XML parse errors as line-anchored markers. The debugger story lights up
through `EditorContext`: `ScriptExecutionPoint` (versioned, poll-friendly - written by the Game page's
debugger drain on Breakpoint/Stepped, cleared on resume/stop) drives the ExecutionLine marker + scroll
in `ScriptPage`; `ScriptValueProbe` (contract-neutral, installed per run) answers hover-values from the
innermost frame. Both are live against the AngelScript + Luau debuggers; Wren stays non-debuggable by
design (no VM debug API).

## Consumers

`ScriptPage` (Wren + AngelScript + Luau, with the debugger seam), `UIDocumentPage` (XML), the future
Shader page (HLSL + DXC diagnostics - this widget is the prerequisite it assumes), and any future text
surface.

## Deferred

Residuals owned by other tracks (HLSL lexer -> shader page, regex search, structured markup-WARNING
line info -> game-ui, IME preedit positioning, the `Editor` vs `Cook/` folder-naming question ->
code-standard track): `Documentation/Backlog/code-editor-followups.md`.

---

Design rationale (the purpose-built-widget-vs-vendored decision, the completion-from-the-start call, the
layering decision, and the phase-by-phase history) is in
`Documentation/Archive/code-editor-design-history.md`.
