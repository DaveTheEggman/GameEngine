# Draconic build specs

Specs for the current backlog, written to be built by an implementing agent
(Opus) with review-only oversight. Each spec is self-contained: context, exact
scope, files to touch, phased plan, acceptance criteria, and known gotchas.

Read CONVENTIONS.md FIRST - it is binding for every spec in this folder.
HANDOFF.md records the review baseline commit (everything after it is the
implementing agent's work).

## Index (rough priority order)

| Spec | Task | Size |
|---|---|---|
| [game-ready-scripting.md](game-ready-scripting.md) | - | L (closed 2026-08-08) |
| [game-ready-scripting2.md](game-ready-scripting2.md) | - | M |
| [smoketest-fixes.md](smoketest-fixes.md) | - | M (10 items) |
| [issues-triage.md](issues-triage.md) | - | M (10 issues) |
| [mcp-agent-access.md](mcp-agent-access.md) | - | L (P0 building) |
| [luau-backend.md](luau-backend.md) | - | L |
| [async-resource-loading.md](async-resource-loading.md) | #123 | L |
| [settings-unknown-section-passthrough.md](settings-unknown-section-passthrough.md) | - | S |
| [web-remainder.md](web-remainder.md) | #112 | L |
| [reflection-track.md](reflection-track.md) | #110 | L |
| [camera-preview.md](camera-preview.md) | #118 | M |
| [gui-tests-keyframes.md](gui-tests-keyframes.md) | #115 | S |
| [sdl-static.md](sdl-static.md) | #119 | S |
| [editor-polish.md](editor-polish.md) | - | M |
| [source-path-p3-p4.md](source-path-p3-p4.md) | - | S |
| [vg-quality-leftovers.md](vg-quality-leftovers.md) | #121 leftovers | M |
| [deferred-by-design.md](deferred-by-design.md) | - | note only |

Sizes: S = a session or less, M = a few sessions, L = a multi-session track
that should land in phases with a green build after each phase.
