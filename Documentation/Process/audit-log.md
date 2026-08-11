# Documentation audit log

> Status: CURRENT
> Track: documentation-system (Documentation/Specs/documentation-system.md)

Append-only record of the docs reorganization (spec: `Specs/documentation-system.md`).
A reviewer can reconstruct every move from here. State guesses are PROVISIONAL - P1
classifies finally and verifies Systems claims against code.

---

## P0 - track first, judge later (2026-08-11)

Reverses the docs-never-committed policy: every former `docs/` file plus the root
handoffs/review is now under `Documentation/`, provisionally categorized WITHOUT
content edits, and committed so nothing more can be lost.

- Created `Documentation/{Systems,Specs,Plans,Backlog,Guides,Process,Archive}/`.
- Removed the 4 doc entries from `.gitignore` (they become tracked):
  `Documentation/Planning/Core.md`, `docs/design/{shaders-materials-hot-reload,runtime-host,renderer}.md`.
- Emptied + removed `docs/` and `Documentation/Planning/`.
- `Documentation/Planning/Core.md` -> `Plans/Core.md` (user ruled 2026-08-11: LIVING planning, not historical).
- NOT touched (stay as-is): `KNOWN_ISSUES.md`, `README.md` at root; the user's `start.txt`,
  `net-demo-scripts.txt`, `smoke-test-checklist.txt`.
- Name collisions disambiguated (two files shared a basename across the old design/ + specs/):
  - `docs/design/editor-polish.md` -> `Backlog/editor-polish-design.md`
  - `docs/specs/editor-polish.md`  -> `Backlog/editor-polish-spec.md`
  - `docs/design/reflection-track.md` -> `Systems/reflection-track.md`;
    `docs/specs/reflection-track.md` -> `Specs/reflection-track.md` (different categories - no rename).

State: `current` (believed true/active) | `stale` (likely outdated / track complete -> Archive in P1)
| `unknown` (needs P1 audit) | `archived` (historical, header-only in P1).

### Systems/ (subsystem reference - all `unknown` until P1 verifies claims vs code)

| New path | Old path | State |
|---|---|---|
| Systems/asset-pipeline.md | docs/design/asset-pipeline.md | unknown |
| Systems/audio.md | docs/design/audio.md | unknown |
| Systems/code-editor.md | docs/design/code-editor.md | unknown |
| Systems/editor.md | docs/design/editor.md | unknown |
| Systems/editor-jobs.md | docs/design/editor-jobs.md | unknown |
| Systems/export.md | docs/design/export.md | unknown |
| Systems/export-reachability.md | docs/design/export-reachability.md | unknown |
| Systems/export-templates.md | docs/design/export-templates.md | unknown |
| Systems/game-instance.md | docs/design/game-instance.md | unknown |
| Systems/game-ui.md | docs/design/game-ui.md | unknown |
| Systems/gui-port.md | docs/design/gui-port.md | unknown (experimental/parked) |
| Systems/input.md | docs/design/input.md | unknown |
| Systems/instanced-mesh.md | docs/design/instanced-mesh.md | unknown |
| Systems/networking.md | docs/design/networking.md | unknown |
| Systems/particles.md | docs/design/particles.md | unknown |
| Systems/particles-authoring.md | docs/design/particles-authoring.md | unknown |
| Systems/path-type.md | docs/design/path-type.md | unknown |
| Systems/physics.md | docs/design/physics.md | unknown |
| Systems/post-processing-config.md | docs/design/post-processing-config.md | unknown |
| Systems/prefabs.md | docs/design/prefabs.md | unknown |
| Systems/project-and-settings.md | docs/design/project-and-settings.md | unknown |
| Systems/reflection-probes.md | docs/design/reflection-probes.md | unknown |
| Systems/reflection-track.md | docs/design/reflection-track.md | unknown |
| Systems/renderer.md | docs/design/renderer.md | unknown (was .gitignored) |
| Systems/runtime-host.md | docs/design/runtime-host.md | unknown (was .gitignored) |
| Systems/script-debugger.md | docs/design/script-debugger.md | unknown |
| Systems/scripting.md | docs/design/scripting.md | unknown |
| Systems/settings.md | docs/design/settings.md | unknown |
| Systems/shaders.md | docs/design/shaders.md | unknown |
| Systems/shaders-materials-hot-reload.md | docs/design/shaders-materials-hot-reload.md | unknown (was .gitignored) |
| Systems/text-scenes.md | docs/design/text-scenes.md | unknown |
| Systems/viewport-input.md | docs/design/viewport-input.md | unknown |
| Systems/web-platform.md | docs/design/web-platform.md | unknown |
| Systems/skinning-benchmark.md | docs/skinning-benchmark.md | unknown |

### Specs/ (active build specs - completed tracks move to Archive in P1)

| New path | Old path | State |
|---|---|---|
| Specs/luau-backend.md | docs/specs/luau-backend.md | current (active track) |
| Specs/mcp-agent-access.md | docs/specs/mcp-agent-access.md | current (active track) |
| Specs/documentation-system.md | docs/specs/documentation-system.md | current (this spec) |
| Specs/game-ready-scripting.md | docs/specs/game-ready-scripting.md | stale (track ~complete) |
| Specs/game-ready-scripting2.md | docs/specs/game-ready-scripting2.md | stale (track ~complete) |
| Specs/reflection-track.md | docs/specs/reflection-track.md | stale (task #110, done) |
| Specs/scene-scripting.md | docs/specs/scene-scripting.md | stale (shipped) |
| Specs/sdl-static.md | docs/specs/sdl-static.md | stale (task #119) |
| Specs/gui-tests-keyframes.md | docs/specs/gui-tests-keyframes.md | stale (task #115) |
| Specs/smoketest-fixes.md | docs/specs/smoketest-fixes.md | stale (weekend pass) |
| Specs/camera-preview.md | docs/specs/camera-preview.md | unknown (task #118) |
| Specs/async-resource-loading.md | docs/specs/async-resource-loading.md | unknown (task #123) |
| Specs/source-path-p3-p4.md | docs/specs/source-path-p3-p4.md | unknown |
| Specs/settings-unknown-section-passthrough.md | docs/specs/settings-unknown-section-passthrough.md | unknown |
| Specs/property-animation.md | docs/specs/property-animation.md | unknown |
| Specs/README.md | docs/specs/README.md | current (specs index) |

### Plans/ (approved designs / future phases)

| New path | Old path | State |
|---|---|---|
| Plans/Core.md | Documentation/Planning/Core.md | current (user: living) |
| Plans/roadmap.md | docs/design/roadmap.md | stale (known-stale; P2 refresh) |
| Plans/renderer-improvements.md | docs/design/renderer-improvements.md | unknown |
| Plans/pie-on-thread.md | docs/design/pie-on-thread.md | current (shelved sketch) |
| Plans/navigation.md | docs/specs/navigation.md | unknown (not started) |
| Plans/terrain.md | docs/specs/terrain.md | unknown (not started) |
| Plans/instanced-crowds.md | docs/instanced-crowds.md | unknown |

### Backlog/ (deferred / queued / triage)

| New path | Old path | State |
|---|---|---|
| Backlog/deferred-by-design.md | docs/specs/deferred-by-design.md | current |
| Backlog/issues-triage.md | docs/specs/issues-triage.md | current |
| Backlog/vg-quality-leftovers.md | docs/specs/vg-quality-leftovers.md | unknown |
| Backlog/web-remainder.md | docs/specs/web-remainder.md | unknown (task #112) |
| Backlog/editor-pages-gap.md | docs/design/editor-pages-gap.md | unknown |
| Backlog/editor-polish-design.md | docs/design/editor-polish.md | unknown |
| Backlog/editor-polish-spec.md | docs/specs/editor-polish.md | unknown |
| Backlog/gui-gaps.md | docs/design/gui-gaps.md | unknown |
| Backlog/parity-2026-08.md | docs/design/parity-2026-08.md | current (2026-08-10 census) |
| Backlog/sedulous-backport.md | docs/sedulous-backport.md | unknown |

### Guides/ (how-to)

| New path | Old path | State |
|---|---|---|
| Guides/adding-facades.md | docs/design/adding-facades.md | current |
| Guides/emscripten-windows.md | docs/emscripten-windows.md | unknown |
| Guides/smoke-checklist.md | docs/smoke-checklist.md | unknown |

### Process/ (working agreements)

| New path | Old path | State |
|---|---|---|
| Process/CONVENTIONS.md | docs/specs/CONVENTIONS.md | current |
| Process/HANDOFF.md | docs/specs/HANDOFF.md | current (review baselines) |
| Process/code-standard.md | docs/design/code-standard.md | current |
| Process/audit-log.md | (new, this file) | current |

### Archive/ (historical - header-only in P1, non-authoritative)

| New path | Old path | State |
|---|---|---|
| Archive/handoff-luau-debugger.md | handoff-luau-debugger.md (root) | archived |
| Archive/handoff-reachability-p1.md | handoff-reachability-p1.md (root) | archived |
| Archive/handoff-scripting-capabilities.md | handoff-scripting-capabilities.md (root) | archived |
| Archive/handoff-scripting-p2.md | handoff-scripting-p2.md (root) | archived |
| Archive/handoff-shaders-web.md | handoff-shaders-web.md (root) | archived |
| Archive/review-14f16a84-HEAD.md | review-14f16a84-HEAD.md (root) | archived |

Total: 79 files (72 former docs/ + 6 root handoffs/review + Core.md).
