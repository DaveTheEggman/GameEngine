# Code Editor - residuals

> ARCHIVED 2026-09-01: still-open items live in Documentation/Plans/week-2026-09-05.md
> ("Backlog folder absorbed"). This archive keeps the full detail.


> Status: CURRENT
> Track: [[code-editor-track]]

The track is complete (`Documentation/Systems/code-editor.md`). These are deliberate leftovers, each
owned by another track; verified still-open against code.

- **HLSL lexer spec + intrinsics completion.** The `CLikeLexer` machinery is ready (a ~20-line spec);
  the tables belong to the Shader page's module and arrive with the SHADERS track (the page itself).
  No `HlslLexer` exists yet.
- **Regex search** in the find bar. Plain substring + case/whole-word shipped; add regex only if real
  usage asks. No regex path in the find bar today.
- **Structured line info for markup WARNINGS.** Parse ERRORS mark their line (via an `XmlDocument`
  probe); the UI loader's unknown-element/attribute WARNINGS are plain strings with no position. Needs
  `MarkupLoader` to carry per-node line info (game-ui track polish).
- **IME preedit / candidate positioning.** Committed text works (the shell bridge); composition UI at
  the caret is net-new core-UI work if an IME-heavy locale needs it.
- **`Script/{Language}/Editor` vs `Cook/` folder naming.** Parked on the CODE-STANDARD track as an
  engine-wide decision (many `*Editor` modules link the cook path; no script-only exception was made).
