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
| [web-remainder.md](web-remainder.md) | #112 | L |
| [reflection-track.md](reflection-track.md) | #110 | L |
| [editor-polish.md](editor-polish.md) | - | M |
| [source-path-p3-p4.md](source-path-p3-p4.md) | - | S |
| [vg-quality-leftovers.md](vg-quality-leftovers.md) | #121 leftovers | M |
| [msaa.md](msaa.md) | I11 | M (P1 built; P1g open) |
| [property-animation.md](property-animation.md) | #129 | L |
| [asset-variants.md](asset-variants.md) | #133 | L (variants axis + texture compression) |
| [mesh-lod.md](mesh-lod.md) | - | M (spec prepared, not scheduled) |
| [terrain-vegetation.md](terrain-vegetation.md) | - | L (P0 splat-driven grass BUILT 2026-09-21; P1 mask + wind and P2 prop scatter not scheduled) |
| [ui-theme-migration.md](ui-theme-migration.md) | #135 | L (P0 shipped; P1-P4 phased) |
| [ui-layout-and-style-model.md](ui-layout-and-style-model.md) | - | XL (PROPOSED: uniform LayoutStyle, real cascade, box model, transitions; P0-P4) |
| [ui-browser-parity-tests.md](ui-browser-parity-tests.md) | - | M (PROPOSED: measured Chrome expectations for layout, not derived ones) |
| [ui-text-and-gaps.md](ui-text-and-gaps.md) | - | L (PROPOSED: shaping + inline runs; the UI gap list vs RmlUi/MewUI) |
| [whiteboxing.md](whiteboxing.md) | - | M (PROPOSED: parametric blockout pieces driving mesh + collider; P0-P3) |
| [paperkid.md](paperkid.md) | - | game plan |
| [documentation-system.md](documentation-system.md) | - | process |
| [scene-prefab-unification.md](scene-prefab-unification.md) | - | WIP design question (needs Fable) |
| [navigation-editor-ui.md](navigation-editor-ui.md) | - | design question (needs Fable): nav P4b editor UI |
| [deferred-by-design.md](deferred-by-design.md) | - | note only |

> Fully-built specs are archived at `../Archive/<name>-history.md` and dropped from this index.

Sizes: S = a session or less, M = a few sessions, L = a multi-session track
that should land in phases with a green build after each phase.
