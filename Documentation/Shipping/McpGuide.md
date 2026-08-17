# Working through the MCP server

The engine's MCP server (`engine-mcp`, the `Tools.Mcp` binary) is the headless authoring
surface: newline-delimited JSON-RPC over stdio, the full project/asset/scene/script
workflow with the editor closed. This is the operating manual for any agent connected to
it - it is itself served as `docs://McpGuide.md`, so you can re-read it over the wire.

## First moves in a session

1. `tools/list` - read the real surface before guessing; descriptions carry the contract.
2. `host_info` - pid (kill a hung host by it) and `buildStamp`. After rebuilding the
   engine, compare stamps: a stale host serves yesterday's engine.
3. `resources/list` - the curated `docs://` shipping docs (Scripting/Assets/Scenes/
   KnownIssues) and, once a project is open, every scene/prefab as
   `project://scene|prefab/<guid>`.

## The ground rules

- **Files are truth.** The tools read and write the project's files directly; no editor is
  involved. Do NOT run against a project that an open editor is actively editing - two
  writers, one set of files.
- **Validate-first writes.** `scene_write`/`prefab_write` refuse invalid content with the
  full report; a refusal is the tool working, never something to bypass.
- **Read before destructive changes.** `asset_uses` before deleting anything;
  `project_health` after - dangling references surface later, not at delete time.
- **Check `known_issues` before re-diagnosing** an odd symptom; if it matches a recorded
  issue, report the match and use its workaround.

## Workflows

**Project**: `project_create` -> `project_open` -> `project_info`. `project_health` is the
one-call soundness sweep (dangling refs, broken sources, cook state); a dirty count alone
is normal - clear it with `asset_cook`.

**Assets**: `asset_import` (OS file -> Sources/ + typed asset) -> `asset_cook`
(incremental; `force` for full). `asset_list`/`asset_info` to inspect either database.

**Scenes**: read with `scene_read` (or the `project://` resource), author XML, loop on
`scene_validate` (xml or guid; `valid` + empty `warnings` = the engine will load it),
then `scene_write`. Prefabs mirror it with a single-root rule.

**Scripts**: `script_api` first - the LIVE bound API per backend (wren | angelscript |
luau); never trust memorized signatures. `script_create` seeds a starter asset
(behavior | level | game tier), then edit the returned source FILE, loop on
`script_validate`, and `asset_cook` to make the class attachable.

**Diagnostics**: `log_write` a marker -> do the risky thing -> `log_read` with
`sinceSequence` = the marker's sequence to see exactly what the engine said after it.

**Export**: `project_health` first (catch breakage before a long cook), then
`project_export` (preset optional; default = first/host preset). The dist lands under
`<project>/Dist` unless `out` says otherwise.

## Per-tool gotchas

- `script_validate` is a COMPILE check (`checkLevel: "compile"`): engine-API calls are not
  type-checked - a misspelled method compiles and fails at runtime. Cross-check with
  `script_api`.
- `scene_validate` warnings mean component records of a type the engine does not know -
  they would be SKIPPED on load. Treat warnings as breakage to fix, not noise.
- `asset_uses` reports DIRECT users only; re-run on a user to walk the chain. An empty
  result plus empty `projectSettingsUses` is the "safe to touch" signal.
- `log_read` is incremental - always pass the previous `lastSequence`; a non-zero
  `dropped` means the ring overflowed and old lines are gone.
- `asset_cook` and `project_export` are long-running calls (a full cook may run inside
  them); do not assume a hang before minutes have passed.
- `project_export` failures say little in the response by design - the detail is in
  `log_read` (Cook/Export categories).

## When something looks wrong

1. `host_info` - is the host the binary you think it is (buildStamp)?
2. `log_read` - what did the engine actually say?
3. `known_issues` - is it already recorded?
4. `project_health` - is the project itself broken?
Only then diagnose fresh - and write a `log_write` marker before retrying so the next
read starts at your action.
