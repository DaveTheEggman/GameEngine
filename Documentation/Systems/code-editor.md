# Code editor — CodeEditView (ui.toolkit)

Status: **TRACK COMPLETE (2026-07-29).** All four phases shipped and user-verified on
screen; the second smoke pass's five findings (two-row find bar, punctuation key bridge,
non-focus-stealing pointer tooltips, completion ranking tiers + overflow indicator,
ToggleButtons) fixed and re-verified. Remaining items live in the Residuals section at the
bottom, each owned by another track. Phase-by-phase history follows.

P1 history: **complete + user-verified on screen (2026-07-28)** - all smoke items passed,
including two first-run fixes (caret auto-scroll vs the ScrollBar's stale-max clamp-and-
write-back; gutter clip against horizontally scrolled text). Shipped: `:code_document` + `:code_edit_view` toolkit partitions, the
`View::WantsTabKey` core input fix (Tab dispatch-first with traversal fallback), bundled
DejaVu Sans Mono as the editor's "Mono" family, ScriptPage + UIDocumentPage migrated
(BreakpointGutter deleted; breakpoints are native markers over the shared EditorContext
store; compile errors land as line diagnostics). 30 new headless tests; toolkit + UI suites
green on clang and gcc. Next: P2 lexers.

**P4 (rich completion + debugger seam) code-complete (2026-07-29), awaiting on-screen
smoke:** ScriptApiCompletionProvider in draconic.editor.script - backend-NEUTRAL over
IScriptManager::DescribeBoundApi (a throwaway manager per language runs the runtime's exact
registration sequence once, lazily; type/namespace names at top level, a type's members
after `Type.`); completion trigger characters on the widget (default "."; the markup page
uses "<"); MarkupCompletionProvider in TOOLKIT (MarkupRegistry is draconic.ui's own - same
generic tier as XmlLexer) over new MarkupRegistry enumeration APIs (CollectElementNames /
CollectAttributeNames); ExecutionLine plumbing end-to-end: EditorContext::ScriptExecutionPoint
(versioned, poll-friendly) written by the Game page's debugger drain (innermost frame on
Breakpoint/Stepped, cleared on resume/stop) and consumed by ScriptPage (marker + scroll) -
lights up on screen when the script-debugger track ships a real debugger. Deferred: the
hover-value seam (needs a live debugger), HLSL intrinsics (arrives with the shader page).

**P3 (diagnostics + ergonomics) code-complete (2026-07-28), awaiting on-screen smoke run:**
find/replace bar (self-drawn floating panel of real controls; case/whole-word; live
highlights; F3 navigation incl. capture-phase interplay while a bar field is focused),
go-to-line (Ctrl+G), comment toggle (Ctrl+/ via ICodeLexer::LineCommentPrefix; XML opts
out), bracket-pair highlight (nesting-aware, cross-line), brace-aware Enter indent,
diagnostic hover tooltips (ITooltipProvider), and UI-document XML parse errors as
line-anchored markers (direct XmlDocument probe for ErrorLine). CodeDocument grew FindAll,
compound edits (one-undo ReplaceAll), and FindMatchingBracket. Deferred from P3: regex
search; structured line info for markup WARNINGS (loader-side work).

**P2 (syntax highlighting) shipped + user-verified (2026-07-28).**
Shipped: the ICodeLexer line-state seam + incremental CodeHighlighter (relex-until-
convergence, lazy frontier), the configurable CLikeLexer + stateful XmlLexer, themed
CodeTokenColors, and styled token rendering in CodeEditView. Layering DECIDED with the user:
toolkit ships lexer MACHINERY + an id-keyed CodeLexerRegistry but ZERO language tables (the
same shape as ICompletionProvider - reflection-fed completion also plugs in from outside).
Language facts live in per-language editor-UI targets in Script/{Language}/Editor/
(Draconic::ScriptWrenEditorUI, Draconic::ScriptAngelScriptEditorUI) - SIBLINGS of the cook
targets there, because those link into DraconicCook/DraconicExport which must stay
widget-layer-free; only the editor app links the UI targets, registering in Main beside the
backend/cook registrations. P4's introspection completion providers get the same homes.
XML is generic - the UI-document page constructs toolkit's XmlLexer directly; HLSL's spec
arrives with the shader page. File layout: everything under UI/Toolkit/Code/ - slim
CodeLexer.cppm interface, per-concern implementation units (CodeHighlighter/CLikeLexer/
XmlLexer/CodeLexerRegistry .cpp), shared scanning primitives in an INTERNAL partition
(:code_lexer_scan - a partition implementation unit; GCC rejects non-exported interface
partitions). Deferred to the code-standard track: whether "X.editor" (the engine-wide
authoring/cook-layer naming - 14 such modules link into DraconicCook) should ever split into
Cook/ folders; decided NOT to make script the lone exception now.

Decided (user, 2026-07-28) as the first of three queued conversations (code editor →
shaders → Emscripten/WebGPU); sequenced first because scripting needs it now and the shader
page will need it.

Decisions:
- **Purpose-built widget** (`ui::toolkit::CodeEditView`), NOT a vendored editor (Scintilla and
  friends assume a foreign rendering/input stack; the port cost ≈ the build cost plus a
  permanent no-STL/-fno-exceptions tax) and NOT more bolt-ons over `EditText` (a form control
  with a flat-string model that blocks highlighting, markers, and large-file performance — and
  that game UI also uses; code-editor complexity does not belong there).
- **Lives in `ui.toolkit`** beside CurveCanvas/NodeGraphCanvas, themed via the palette.
- **Completion is in scope from the START** — the popup + provider seam shape the widget's
  input routing from day one (keys route through the open popup first), not a later add-on.

## Current state (what this replaces)

`ScriptPage` = multiline `EditText` + an external `BreakpointGutter` reading its line metrics
and scroll offset. `UIDocumentPage` = the same `EditText` for XML. One flat `String`, full
reshape per edit, single-style draw, no marker model.

## Architecture

- **Buffer**: an `Array<String>` of lines. Source files are KBs, not MBs — a line array is
  cache-friendly, trivially virtualizable, and maps 1:1 to lexer/marker state. Cursor and
  selection are (line, column) pairs.
- **Rendering**: virtualized — only visible lines are shaped/drawn (the ListView lesson).
  Monospace fast path: column = x / advance makes hit-testing, cursors, and column selection
  cheap. VG-drawn like every toolkit widget; token styles resolve through the theme palette.
- **Editing core**: cursor/selection/clipboard, word motion, line ops (dup/move/join),
  auto-indent (copy previous line's leading whitespace + language hook), bracket matching,
  comment-toggle (language hook), and an UNDO stack of edit deltas (insert/remove spans with
  cursor state) — coalescing consecutive typing like EditText's behavior does.
- **Gutter + markers**: first-class per-line marker flags (Breakpoint, Error, Warning,
  ExecutionLine, Modified) + a line-number gutter with a clickable margin. `ScriptPage`'s
  breakpoints become native markers; the AngelScript debugger's current-line highlight
  (script-debugger.md P1.5) renders through ExecutionLine; compile diagnostics (script cook,
  DXC, UI-document parse) inject Error/Warning markers with messages surfaced as row tooltips
  and an optional squiggle underline on the token range.
- **Lexing** (`ICodeLexer`, line-state model — the Scintilla insight): the lexer receives one
  line plus the ENTRY state (e.g. "inside a block comment / raw string") and returns styled
  token spans plus the EXIT state. Each line caches its entry state; an edit re-lexes from the
  edited line downward until exit states converge (usually 1 line). Lexers are small
  hand-written tokenizers (~100-200 lines each): **Wren, AngelScript, HLSL, XML** to start.
- **Completion** (in scope from the start):
  - `ICompletionProvider`: `(buffer, cursorLine, cursorColumn, triggerKind) -> candidates`
    (label, insert text, kind icon tag, optional detail). Providers are per-language and
    COMPOSABLE — a document-word provider (identifiers harvested from the buffer) backs every
    language immediately; richer providers layer on top.
  - Popup: anchored at the cursor, filtered as the user types, arrows/Tab/Enter/Escape routed
    to the popup while open (this is why it shapes P1's input design), never steals focus.
  - Trigger model: identifier characters (after N chars), explicit Ctrl+Space, and
    language trigger characters ('.', '::').
  - Language backends: **AngelScript first** — the script track's introspection seam already
    reports the backend's ACTUAL bound API surface; **Wren** via the introspection battery +
    document words; **HLSL** keywords/intrinsics + snippet completions; **XML(UI documents)**
    tag/attribute vocabulary from the UI cook's known-attribute tables (the same tables that
    produce "unknown attribute" warnings).
- **Find/replace**: an in-widget bar (find, replace, case/word/regex-lite), match highlight
  markers, F3/Shift+F3. Go-to-line (Ctrl+G).

## Phases

- **P1 — core + shell integration.** Buffer, virtualized monospace render, cursor/selection/
  clipboard/undo, gutter + line numbers + marker model, popup INFRASTRUCTURE (anchoring +
  key routing) with the document-word completion provider. `ScriptPage` and `UIDocumentPage`
  migrate; breakpoints become native markers. Headless tests: buffer ops, undo coalescing,
  marker bookkeeping, word-provider results.
- **P2 — lexing + languages.** ICodeLexer seam + Wren/AngelScript/HLSL/XML lexers + themed
  styles + incremental relex convergence tests.
- **P3 — diagnostics + ergonomics.** Compiler error/warning markers wired end-to-end (script
  cook, DXC when the shader track lands, UI parse), find/replace + go-to-line, auto-indent,
  bracket match, comment toggle.
- **P4 — rich completion + debugger.** AngelScript introspection provider, Wren provider,
  HLSL intrinsics, XML attribute vocab; ExecutionLine + hover-value seam with the debugger
  track (P1.5 consumer).

## Consumers

ScriptPage (Wren + AngelScript, debugger), the future Shader page (HLSL + DXC diagnostics —
this widget is a prerequisite the shader discussion can now assume), UIDocumentPage (XML),
and any future text surface (log viewers, scene-XML inspection).

## Residuals (deliberate leftovers, with their owning tracks)

- **ExecutionLine + hover-values: RESOLVED (2026-07-29).** The AngelScript debugger was
  already live (suspension-based, battery-certified) - the earlier "backends return null"
  belief came from a stale interface comment. The full editor story now exists: paused
  location -> ExecutionLine marker + scroll, and hover-values via the contract-neutral
  EditorContext::ScriptValueProbe (Game page installs it per run; ScriptPage feeds the
  hovered identifier; innermost-frame locals answer). Awaiting one on-screen smoke run.
  Wren remains non-debuggable (no VM debug API) - by design.
- **HLSL lexer spec + intrinsics completion** — the CLikeLexer machinery is ready (a
  ~20-line spec); the tables belong to the Shader page's module and arrive with the
  SHADERS track P4 (the page itself).
- **Regex search** in the find bar — plain substring + case/whole-word shipped; add
  regex-lite only if real usage asks for it.
- **Structured line info for markup WARNINGS** — parse ERRORS mark their line (XmlDocument
  probe); the loader's unknown-element/attribute warnings are plain strings with no
  position. Needs MarkupLoader to carry per-node line info (game-ui track polish).
- **IME preedit/candidate positioning** — committed text works (the shell bridge);
  composition UI at the caret is net-new core-UI work if an IME-heavy locale needs it.
- **`Script/{Language}/Editor` vs `Cook/` folder naming** — parked on the CODE-STANDARD
  track as an engine-wide decision (14 `*Editor` modules link DraconicCook; no script-only
  exception).
