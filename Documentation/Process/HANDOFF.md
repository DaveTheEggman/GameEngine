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

---

## Review pass 6 (2026-08-11, Fable): Luau P6 debugger + bytecode consumption + gating - PASS

Range 0140010b..e7d617f5 (10 commits). Full battery: 117 targets, clang +
gcc, ZERO failures; ASAN clean on Script.Luau.Tests + Engine.Script.Tests
(the executor changed - sanitizer mandatory).

- Q1-Q6 debugger rulings all traced to implementation: pooled lua_resume
  executor with NO dual path (the remaining pcalls are chunk BODIES at Load
  + waitUntil predicates - module-scope breakpoints defer via the Q3 guard;
  now documented in the spec's nuance ledger), per-class chunks, held-thread
  discipline (reset only on pool return), shared resume router
  (ClassifyResume/ProcessCoroutineResume - both consumers verified).
- ACCEPTED DEVIATION from Q2: breakpoints are a linear array scan, not a
  hash - at realistic counts while attached this is noise; revisit only
  with debug-session profiling evidence.
- Bytecode consumption both backends (blob-preferred, debuggable: Luau
  debugLevel 2, AS keeps debug info + sourceName section); editor Luau
  lexer + the found-and-fixed missing Luau cook-creator registration;
  gating matrix (all-on verified here by the battery), Wren vendored by
  copy.
- DEFERRED by user: P7 Wren retirement (gating settled first - removal
  stays surgical), P5b external luau-analyze.

Baseline: e7d617f5. (Note: the Documentation/ P0 migration landed between
review request and this record - paths in older passes refer to docs/.)

## Review pass 7 (2026-08-15, Fable): MSAA P1 + EntityRef + coverage-gap closure - PASS

Range 0a45edce..44827741 (121 commits). This pass also formally closes two
stretches never inside a recorded pass range:

- 0a45edce..0140010b (27): Luau P2-P5 (native Float3 vector, reflected
  enums, Level tier + scene event bus, Roll Call acceptance, .d.luau
  emitter), the overloadedName contract + shared conformance battery, the
  debrand rename, and two AngelScript fixes. Much of this was reviewed
  interactively as it landed (the overload contract and Luau doc rulings
  were Fable's); what had never been verified was sanitizer coverage of
  the AngelScript/Wren delegate work - now done (below).
- e7d617f5..a4546c77 (45): mostly Fable's own session work (mesh sidecar
  v3, async binds + settle cascade, mip generation, I2 capture fix, MSAA
  spec) + the Opus Docs P0/P1 audit + 4 Windows-support commits
  (01f8aa3a, 427fe986, 3bb34756, 05a1764a - inspected, benign).

Verification: full battery at HEAD, 117 targets, clang + gcc, ZERO
failures. ASAN: Script.AngelScript.Tests (30/30) + Script.Wren.Tests
(22/22) clean - covers a73ce62d (AS coroutine delegate release), e0c09ab0
(`?&in` reference-arg read), and 4f4604d0 (Wren delegate teardown +
reachability closure). Luau + Engine.Script were sanitized in pass 6.

MSAA P1 (e956774d..a0213f1c) - every spec ruling traced to implementation:

- SV_Depth into a real depth-format target; sample-0 depth/aux resolve
  ("NEVER averaged" honored); scene color via the fixed-function resolve
  attachment (empty pass, no blit, both backends).
- Resolve lands after opaque+sky, before SSR/AO/TAA; transparent + world
  UI run 1x on resolved color. PSO matrix scoped to Opaque-affinity
  passes only. msaaSamples==1 aliases the handles - byte-identical to the
  old path, no resolve passes emitted.
- ACCEPTED DEVIATION: decals moved AFTER the resolve, sampling the 1x
  resolved depth - WebGPU forbids sampling a multisampled texture, and
  sample 0 of the MSAA depth IS the resolved depth, so reconstruction is
  identical; still before SSR/AO/TAA. Rationale documented in-place.
- WebGPU's non-contiguous {1,4} sample set: exact-count
  SupportsMsaaSamples (device query + ceiling + resolve-pass presence)
  with snap; multisampled aux bound UnfilterableFloat (.Load, no sampler
  - required by WebGPU, correct everywhere).
- OPEN: P1g pixel-probe acceptance (1x vs 4x edge coverage + transparent
  no-regression). Opus resumes there.

EntityRef (a8e39cb9..8bdcc691): wire-identical to a bare Guid by
construction (ADL Serialize writes only the inner guid) - the
JointComponent migration needed no version bump, verified. Dumb guid
holder (no cached handle) honoring the re-resolve-never-borrow rule.
Prefab remap is reflection-driven with both semantics tested
(intra-prefab remaps to the instance copy; external refs preserved).
Modal entity-tree picker replaces the flat menu.

Baseline: 44827741. Post-record Fable doc commits e835c561 (this record) +
48fdb80d (UAT audit pass) are also reviewed-by-construction, so the NEXT
review pass starts at 48fdb80d. Opus resumes from here (MSAA P1g first,
then property animation per Documentation/Specs/property-animation.md).

## Review pass 8 (2026-08-15, Fable): MSAA P1g/P2 + asset-variants P1+P2core + UAT fixes - PASS

Range 8ceb3a1e..fd8b04e0 (33 commits; a handful are the user's UAT notes and
Fable's roadmap). Full battery at HEAD: 118 targets x clang + gcc = 236 runs,
ZERO failures. No script/executor changes - no ASAN pass required.

- **MSAA P1g DONE + P2 progress:** RHI.TestSupport (backend-agnostic offscreen
  probe substrate, adopted by Render.Backend + VG.Backend suites - good
  consolidation), the 4x-vs-1x silhouette coverage probe + post-stack
  composition probe, kMsaaLevels single source of truth, renderMsaaSamples
  project setting -> player + settings UI. MSAA P1 acceptance is now CLOSED.
- **asset-variants P1 COMPLETE:** bc7enc + astcenc vendored with distinct
  archive names (thirdparty_*, the imgui lesson honored), SYSTEM includes,
  cook-time-only linkage; astcenc forced scalar-ISA so one lib builds x86 +
  wasm. Texture.Compression module + Decision-5 policy table; builder v3
  encodes post-mip-chain; block-aware upload (bytesPerRow = block rows);
  BC1/BC7/BC5 GPU sample probe green on Vulkan + WebGPU, ASTC probe
  self-skips on BC-only desktop GPUs. Texture-page Usage/Compression knobs +
  the _normal-suffix import heuristic (round-trip tested).
- **asset-variants P2 core (a-e) verified:** Variance() + CookTarget (a
  CAPABILITY struct - matches the Decision-3 ruling), platform salt that
  propagates through READ-DEPS (reader-of-variant recooks; tested),
  ContentDatabase::CopyContentForward, Tools.Cook --target. NOTE: per the
  Q2 ruling the per-target DB root moves from Cooked/<id>/ to a sibling
  Cooked-<id>/ (recursive-pack-walk hazard Opus itself flagged) - P2e
  adjustment expected in the next batch, with the byte-identical desktop
  pack test.
- **P2f/P3 rulings delivered** (4031a0fa, inline in the spec): adapter probe
  inside MakeOptions under ASYNCIFY (no lifecycle reorder); two complete
  paks (Content-bc/-astc); player.xml contentVariants ABSENT on desktop;
  scan-once reachability with a pak guid-set equality assertion; shaders.dpak
  stays single; export reuses CookForTarget.
- **UAT-session fixes verified:** VG per-frame-slot buffer growth (grows only
  when the slot is free; direct-bound so no descriptor fixups - documented),
  VFS CreateDirectory + persistent empty groups, reusable PageToolbar +
  EditorPage::DiscardChanges (Sound Cue first adopter; its Undo/Redo gap is
  a logged follow-up - edits bypass the command stack).

Baseline: fd8b04e0 (+ Fable doc commits 4031a0fa and this record).

## Review pass 9 (2026-08-15 late, Fable): asset-variants P2f + P3 - PASS

Range 03e313c7..456907b6 (4 commits). Full battery: 118 targets x 2 = 236
runs, ZERO failures. Wasm verified: Engine.Player.html links clean with the
new boot probe (built here, not just claimed).

- Q2 executed: per-target DBs at sibling Cooked-<id>/; the byte-identical
  desktop-pack guard is a real test (junk-filled sibling present ->
  identical Content.pak). Export reuses CookForTarget via CookVariantTargets
  (Q4.4); shaders.dpak stays single (asserted); closure scanned once and
  applied to both variant paks.
- Web boot (65db811c): SelectAndFetchContentPak - preloaded-pak
  short-circuit, throwaway-backend adapter probe torn down before app boot,
  BC preferred, LOG_INFO naming the choice, 404-fallback to Content.pak via
  FileExists-after-wget. Sound.
- Q3 DEVIATION ACCEPTED (amended in the spec): manifest-FREE pak-name
  convention (Content-bc/-astc, no Content.pak in a variant dist). Stronger
  than the original ruling on the byte-identity constraint; conditions:
  fallback exercised in the UAT browser check, and - per the user's
  declared roadmap (quality tiers at least, locales later) - the MANIFEST
  IS PLANNED, landing with the first choice-driven variant dimension
  (probe can't resolve tier/locale). Do not build it before then.
- NOTE for next batch: add the bc-vs-astc pak GUID-SET EQUALITY assertion
  (Q4.1's forever-tripwire) to the two-pak export test - the closure is
  applied once so divergence is structurally unlikely, but the assertion
  is cheap and permanent.
- REMAINING on the track: the browser functional check (desktop loads BC,
  mobile/forced-capability loads ASTC) = UAT, gated on the mobile-web
  smoke precursor.
- Also this pass: camera-preview spec stamped BUILT-2026-08-03 (was never
  stamped; task #118 closed - only on-screen verify remains, UAT).

Baseline: 456907b6 (+ Fable doc commits after it).
