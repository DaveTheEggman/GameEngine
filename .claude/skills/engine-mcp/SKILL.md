---
name: engine-mcp
description: Launch and operate the engine's MCP server (Tools.Mcp) from the engine checkout - build/wiring recipe here, then the operating manual served by the host itself. Use when working on a game project through the engine headlessly.
---

# The engine MCP server (in-checkout launch)

`Tools.Mcp` is the engine's headless MCP host. This skill covers what only makes sense
INSIDE the engine checkout - building and wiring the host. The operating manual
(workflows, validation loops, per-tool gotchas) is a SHIPPING doc the host serves to any
connected agent: read `docs://McpGuide.md` via `resources/read` right after connecting
(on disk: `Documentation/Shipping/McpGuide.md`).

## Build + wire

- Build: `cmake --build build/clang --target Tools.Mcp -j4` (binary:
  `Bin/Debug/Linux64-Clang/Tools.Mcp`).
- Wire: `claude mcp add engine -- <repo>/Bin/Debug/Linux64-Clang/Tools.Mcp` (stdio; the
  server identifies as `engine-mcp`).
- Run it from inside the checkout (or with cwd in it): the `docs://` resources and
  `known_issues` resolve `Documentation/Shipping/` by walking up from the executable,
  then the cwd.
- STDOUT is the wire; engine logs go to stderr.

## The two rules that live here

- After rebuilding the engine, compare `host_info`'s `buildStamp` - a host started before
  the rebuild serves yesterday's engine. Restart it.
- Do NOT point the host at a project an open editor is actively editing - files are
  truth, and two writers share one set of files.

## First action after connecting

`resources/read` -> `docs://McpGuide.md`, then follow it. Keep BOTH documents honest the
way HANDOFF.md is kept honest: when tools change, the guide (and this skill, if wiring
changed) updates in the same commit.
