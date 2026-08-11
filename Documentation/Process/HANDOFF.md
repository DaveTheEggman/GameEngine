# Review handoff - Opus build phase

## Baseline

Last Fable commit before the Opus build phase:

    43beeaed Engine.UI: transparent edge border on world-panel textures (silhouette AA)

Everything AFTER this commit on master is Opus' work. Review scope:

    git log --oneline 43beeaed..HEAD
    git diff 43beeaed..HEAD --stat

## State at handoff (2026-08-02)

- Task #121 (VG stencil-then-cover + MSAA) CLOSED: canvas/world-panel 4x MSAA
  (c1a161d5), sRGB double-decode fix (16107056), world-panel silhouette border
  (43beeaed) - all user-verified on screen.
- Draconic.VG.Backend.Tests = the pixel-probe suite on real Vulkan + WebGPU
  (fills, clip, color pipeline, spreads, blends, MSAA edge coverage). It is
  the regression net for anything touching VG rendering - run it in review.
- All suites green on clang + gcc at the baseline commit.
- Theme reauthoring was DROPPED from the backlog (user verified themes render
  as authored after the double-decode fix).

## Review checklist per Opus PR/commit range

1. Spec compliance: the work matches its spec in this folder, including the
   acceptance section. No scope creep into deferred-by-design.md items.
2. CONVENTIONS.md: both compilers green (gcc module hygiene especially -
   reflection bodies and heavy headers in implementation units), doctest
   coverage present and meaningful, no em-dashes, PascalCase, no
   Co-Authored-By trailer, docs/ never committed.
3. Suites: run the affected module tests on BOTH compilers plus
   Draconic.VG.Backend.Tests if anything near VG/render moved.
4. User-facing changes: list what the user should visually verify - never
   claim visual correctness from code.

---

## Review pass COMPLETE (2026-08-08, Fable)

All 102 Opus commits from the 43beeaed baseline reviewed on branch
`game-ready-scripting`. Fable follow-up commits: eb8aeb7a (completed the
paused in-flight physics setPosition work), 88df78f0 (legacy type-name
fallback + RemoveDirectoryRecursive - the debrand wire-compat fix, see
KNOWN_ISSUES.md). Full battery: green on clang + gcc across every current
test target except the known #115 GUI keyframes; ASAN + TSAN clean on
Core/Resource. Stale old-name test binaries purged from Bin (battery recipe:
run CURRENT ninja targets only). New baseline for any future review:
88df78f0.

## Review pass 2 COMPLETE (2026-08-08 evening, Fable)

Reviewed 27 commits (the Windows agent's msvc branch merged + Opus'
smoketest fixes rebased on it, 3a02bdc7..8eee6c30). Full battery: 106
targets GREEN on clang + gcc INCLUDING Draconic.GUI.Tests - the Windows
agent's use-after-move fix (6e4b468c) killed the long-standing keyframes
failures AND the "GUI segfault" (a failed REQUIRE running into a null deref
under no-exceptions doctest). Task #115 CLOSED. Also verified: the /W4
sweep is behavior-safe (real defects + shadow renames), the module-interface
hygiene commits (JobSystem <thread>, Xml re-export, generic lambdas) are
correct and well-reasoned, smoke fixes 1-3/5-7 match their specs (the
FontAssetImporter was genuinely never registered - the user's report was
exact). One transient gcc ICE during review was concurrent-build contention
(two agents, one build dir) - not code. OPERATIONAL NOTE: coordinate build
windows when two agents share this machine. New review baseline: 8eee6c30.

---

## Review pass 3 (2026-08-08 night, Fable): MCP P0 + triage fixes - PASS

Reviewed 3210ea8d..HEAD (7 commits). P0 Pipeline reorg: structure correct
(Pipeline.Core/Pipeline.Cook + 12 lib pipelines + ModelImporter + script
pipelines split from script editor UI), RTTI identity strings PRESERVED
(the debrand lesson applied - cooked assets keep loading), zero UI deps in
Pipeline libs. NOTE/refinement: Pipeline libs link Editor.Core (verified
UI-FREE - project/import/export plumbing), so the headless bar holds, but
the NAME now misleads; consider moving the importer framework into
Pipeline.Core and leaving Editor.Core truly editor-only - fold into MCP P1
if cheap, else later. Collision-cook root cause found WITHOUT the blocked
user repro (9f7c6829: builder got the cooked PRODUCT but Cast the SOURCE
type; the old test masked it by pointing ctx.db at a source db) +
regression test. I2/I8/I10 per spec; I7 partial-with-reason (engine knob +
page controls in; editor-wide persisted preference deferred, routing
constraint documented). Battery: 106 targets, clang+gcc, ZERO failures.
Baseline: the pushed HEAD.

---

## Review pass 4 (2026-08-09 evening, Fable): debrand completion + json + MCP P1 slices - PASS

Range 60f0a252..HEAD (24 commits; 3 are Fable's own). Full battery: 111
targets, clang + gcc, ZERO failures. Highlights:

- **foundation.json** (b7312b5a): every recorded review point addressed BY
  DESIGN - owned-copy accessors (the interior-pointer hazard), no
  ISerializer backend (policy structural), impl-unit reflection, f64
  numbers. 90 assertions green. The C++-side by-ref Keys() is correctly
  absent from the reflected surface (KeyAt/Count is the script iterator).
- **foundation.mcp** (35caaf10): maps 1:1 onto the binding protocol
  appendix, pinned protocolVersion 2025-06-18. Design refinement WORTH
  KEEPING: tool handlers return Result<JsonValue, String> so the isError
  result carries the real message text, not an ErrorCode.
- **Tools.Mcp + reflection/project tools + Integration.Mcp golden**
  (4bdf91ac, 4e3b73e3): VERIFIED LIVE over stdio in review - version
  negotiation, {tools,resources} caps, string-id preservation, -32602
  naming the offending field, garbage line -> -32700 with the loop
  SURVIVING, batch -> -32600, and type_info(Float3) returning real
  reflection (Dot/Length/Normalized, x/y/z). The protocol is real, not
  just claimed. StderrSink keeps stdout pure.
- Debrand string passes: wire-safety held throughout (template-id +
  user-data prefixes moved to CMake with values UNCHANGED; identity moves
  carried fallbacks). The one casualty was the sanitizer-suffix rename
  (KNOWN_ISSUES; tripwired in pass 4's interleaved Fable work).
- Conventions: zero trailers, zero added dashes across the range.

NEXT for the MCP track: the pipeline tool slice (asset_import/asset_cook
over the Pipeline.Importer seam), scene read/write tools, script_api +
script_validate, resources. The server is ready for Claude Code dogfood
config NOW (stdio: `Bin/Debug/Linux64-Clang/Tools.Mcp`). Baseline: HEAD.

---

## Review pass 5 (2026-08-09 night, Fable): MCP asset tools + script surface + domains - PASS

Range 3f6c1ec7..0a45edce (6 commits). Full battery: 114 targets, clang + gcc,
ZERO failures. Live stdio verification against the fresh binary.

- asset_list/asset_info (3f6c1ec7): clean walk over the content DB; guid
  buffer use verified against Guid::ToChars's null-termination contract.
- Pipeline.Registration (b26ca85f): executes the ruling exactly - superset
  canon (folds the Export-only SceneDocument drift), lean interface BMI,
  count tripwires (kBuilderCount=21/kImporterCount=6), all hosts migrated.
- asset_import/asset_cook (f61bc4dd): rides ImportContext + the null
  deferredWrites headless path (inline writes - the documented contract), cook
  via CookDriver with proper Sources/Cache mounts. Files-are-truth holds.
- script_api (394b9a15): resolves backends LIVE at call time - never cached,
  per the ruling's truth principle.
- Engine.ScriptSurface (fe1bd7c9): ruling A executed faithfully including the
  scope note (runtime keeps per-subsystem registration), impl-unit fan-in,
  tripwire kSubsystemFacadeNameCount=30 + idempotency test, golden asserts
  gameplay facades headless. Luau rider in: RegisterLuauScriptBackend.
- Domain addendum (0a45edce): sweep COMPLETE (zero Editor domains left in
  Code/Pipeline - grep-verified), namespace-scan tripwire catches future
  drift, {domain, inPlayer} emitted per type.

Live checks: script_api(luau) -> Audio/Ui/SceneLoader/Net/Entity present;
TextureAsset -> {domain: Pipeline, inPlayer: false}; Float3 -> {Runtime,
true}. KNOWN GAP (documented, not a regression): RigidBodyComponent absent
from the LUAU dump only - the precursor emitter still binds the
simple-constructible filter, so constructor-less .of handles wait on Luau P2
(the cross-backend diff surfacing exactly this gap is the feature working).

Also this pass: Fable ruling recorded in luau-backend.md on overloaded
methods (B / dedicated overloadedName field / one surface across ALL
backends / registration-time validation mandate). Opus unblocked.

Baseline: 0a45edce.
