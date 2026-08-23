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

## Review pass 10 (2026-08-16, Fable): property animation A-H + DX12 trio - CONDITIONAL

Range 456907b6..d0451152 (51 commits; Fable's UI-theme commits excluded from
review scope). Battery at HEAD: 122 targets x clang + gcc, ZERO failures.
ASAN: PropertyAnimation.Tests + Engine.Script.Tests +
Editor.PropertyAnimation.Tests all green. Verdict: architecture is RIGHT
across all three tracks (wire symmetry, binding re-walk discipline, H1/H2
seams, gesture->undo mapping, DX12 diagnosis) but each carries required
fixes - the track does NOT close until they land.

**Property animation - REQUIRED FIXES (list mirrored in the spec for Opus):**
1. Phase G surface is DEAD: PropertyAnimatorComponent missing from
   RegisterAnimationScriptFacade's components[] (no registry/root/name).
   Verified by direct read. Add + a test that asserts the REFLECTED surface.
2. Empty-channel zeroing: Track::Sample evaluates every channel (empty
   curve = 0); "+ Track" creates all-empty channels -> first tick teleports
   the entity. Needs per-channel active semantics or read-modify-write.
   Same class in the editor: key-add on a multi-channel canvas seeds
   DefaultValue 0 for sibling channels (should seed the curve value at t).
3. Track-kind vs leaf-type mismatch = silent per-frame no-op (WriteBinding
   status discarded; no warn/disable). Extend the disable-once policy.
4. In-scene preview cannot preview Transform tracks (manager-lookup only;
   the runtime's kTransformComponentName branch was dropped in the fork) -
   the DEFAULT track type previews as a no-op and the H4 overlay marker
   never draws. Share the apply loop or port the branch.
5. ClipEditCommand holds ClipEditorView* on the scene page's DURABLE undo
   stack while the view is activation-scoped (panel Sync destroys it):
   undo after tool-deactivate = use-after-free. Verified by direct read.
   Same class: the asset-picker OnPicked captures the panel raw.
6. Preview snapshot staleness: track-set changes mid-preview leave the
   scene modified after StopPreview (re-snapshot on track-identity change).
7. Smaller: AddTracksFromSelection missing LockGroup (undo coalescing);
   clip.loop is dead wire (component loopMode is the only truth - delete or
   make it the default); builder Version() not declared (codebase-wide gap,
   9/22 builders - fix here, sweep later); WriteBackTrack flattens per-key
   interpolation track-wide; wire hardening (trackKind range guard,
   truncated keyInterp should default Linear not Constant).
8. Test debt: PingPong + negative speed + component Serialize round-trip +
   the spec's physics-gotcha test + the four canvas seam properties (all
   3 passing checks are UNGUARDED by tests; box-drag N/A - never built).

**Seam checks (queued at CurveCanvas adoption): Constant-mode fidelity
PASS; time-domain round-trip PASS for no-edit (per-gesture ~1ulp re-domain
drift on touched tracks - acceptable, documented); one-undo-step-per-op
PASS (m_gestureDirty guard is correct; no dead steps); MaxKeys=64
per-instance PASS.**

**DX12 trio (Windows Opus) - right fixes, three follow-ups routed to the
next Windows session (this box cannot compile DX12):** ResolveTexture's
transitionAll reads currentState() which lies in per-subresource mode and
setState erases the truth (verified by direct read; same latent flaw at the
whole-resource TextureBarrier + DxTransferBatch - one uniformity accessor
fixes all three); GenerateMipmaps settles layer 0 only (array/cube stays
non-uniform); Blit still hardcodes COPY_* states (comment-contradicted,
test-only callers). Docs amended THIS pass: KNOWN_ISSUES DX12 entry
rewritten (implementation closed, run + fixes pending), msaa.md stale
overrides paragraph corrected. Deferred observations recorded by the
reviewer: MSAA format list duplicates render constants (drift risk),
per-frame CheckFeatureSupport calls (cache a bitmask at init), Stencil8
reports as R8_UINT capabilities.

Baseline for pass 11: d0451152 (+ Fable doc commits after it).

## Review pass 11 (2026-08-18, Fable): navigation P0-P5 (full track) - PASS with one fix

Scope: everything since pass 10 on the navigation track through eadc76d6
(P4b-3 gizmo + inspector Bake, P5 demo A + NavigationZone ->
NavigationZoneResource rename, profiling scopes, the Navigation Zone creator
placement), reviewed against the navigation-editor-ui.md rulings trail.

**Verified:**
- P4b-3 matches ruling (B) exactly: NavMeshZoneGizmoRenderer in the builtins
  (read-only entity-oriented extents box, DrawWhenUnselected false, mirrors
  Decal); Bake = a NavMeshZoneComponent entry in the central InspectorView
  dispatch calling BakeNavigationZone with ALL outcomes loud via Notify
  (no project / no assigned zone asset / baked / no walkable geometry). No
  hollow registrar shipped. GCC hygiene split (NavigationBakeImpl) correct.
- The NavigationZone -> NavigationZoneResource rename is WIRE-SAFE: the
  runtime product is Object-derived (in-memory RTTI keying only); both
  serialized identities (NavigationZoneSource, NavigationZoneAsset) kept
  their names; cooked DBs are regenerable. No legacy-name fallback needed.
- Creator placement fix (eadc76d6) follows the Physical Material precedent
  (data-asset creators live in Tools.Editor Main, not RegisterSceneEditor);
  Editor.Scene's redundant pipeline link dropped.
- Profiling commits are macro-scope additions in impl units / light headers;
  precedent (audio) already includes Profiler.h in interfaces.
- P5 demo A: WebScene inline runtime bake (NavigationMeshBuilder) + 6-agent
  crowd; Recast compiles for wasm ungated. User-verified on screen.

**The one finding (FIXED in-review): the DefaultApp factory-count tripwire
fired on the FULL battery** - P3b added NavigationZoneFactory (the 20th
standard factory) without bumping kStandardHeadlessFactoryCount (still 19).
The tripwire worked as designed; the miss shows P3b's "green" was per-target,
not the full battery. Bumped 19 -> 20 with the incident noted in the comment.
STANDING REMINDER (re-learned): a landing's green claim means the FULL
two-compiler battery, not the touched targets.

Full battery after the fix: ALL_GREEN clang + gcc (every *Tests binary).
Remaining user steps (not review blockers): the editor-side on-screen pass of
the Bake button + zone gizmo + inspector (P5's open item).

Baseline for pass 12: eadc76d6 + this pass's fix commit.

## Review pass 12 (2026-08-19, Fable): Wren retirement + scripting2 P2 + game-UI kit + preview track + PaperKid P0 + audio cluster - PASS

Scope: 96 commits since pass 11 (8222cc25..a7b951f7), seven tracks. Full
two-compiler battery ALL_GREEN (145 test binaries each); ASAN run over the
whole script set (Script/AngelScript/Luau/Engine.Script/ScriptSurface/
UI.Script) - clean, per the script-changes rule.

**Verified against the rulings trail:**
- Wren retirement (1b220034): ZERO traces - code, CMake options, ThirdParty
  all clean. Backend set is AngelScript + Luau.
- game-ready-scripting2 P2: SceneLoader fully absorbed then deleted (zero
  references); the ScriptName alias mechanism matches the ruling exactly
  (attribute-based, FinalizeTypes duplicate-name trap covering class names
  AND aliases incl. reserved `run`, tests in both backend suites);
  kSubsystemFacadeNameCount consistent at 32 (net zero: -SceneLoader -Ui
  +run +ui) and the check caught the lowercase-`ui` spelling (87042268) -
  third save for that tripwire.
- game-UI kit P1/P2: ScreenStack mutations route through
  MutationQueueRef().QueueAction (finding A honored; onComplete tree
  mutations too, 21cbcf0a); the finding-B stale-handle test exists verbatim
  (UiScriptHandleTests: detached-but-alive, safe no-op, releases on drop);
  the root binding moved to engine.ui.script's own per-context service - a
  sound spec correction (tiers are engine-only).
- Preview track (a-h): complete; the only hand-rolled loops left are the
  scene/game pages (out of scope by ruling) + ONE overlooked consumer (see
  findings). Sim-gating coupling handled correctly: the clip/animgraph pages
  drive their own transports (SampleClip/players - no manager dependency);
  the particle page correctly enables preview-scene simulation.
- Audio cluster: empty-cue = the ruling verbatim (valid empty product +
  draft-state hint + project_health warning + Integration.Mcp test) with the
  builder Version 1->2 bump SAME COMMIT. Audition stack/stop/pause +
  AudioSource visibleWhen declutter landed.
- PaperKid P0: tracked at SampleProjects/PaperKid (not the untracked root
  dirs); scene sources are text envelopes + binary sidecars (correct);
  playable in-editor with the scripted screen flow.

**Findings (minor, queued - none block):**
1. FIXED 2026-08-19 (user: the seeding path was the bug): the project-seed /
   New>Primitive path (CreatePrimitiveMeshInstance, Tools.Editor Main) wrote
   meshes with a raw WriteObject, bypassing WriteMeshAsset - "the one writer
   every save path uses". Now routed through WriteMeshAsset (envelope +
   binary geometry sidecar). PaperKid's already-committed meshes stay
   legacy-inline (the reader supports that form); they flip to sidecar on
   their next save/re-seed.
2. RETRACTED (user correction 2026-08-19): UIDocumentPage is a UI-document
   preview, NOT a 3D viewport - it is not a PreviewViewport consumer. The
   finding was wrong; no migration queued.
3. RESOLVED 2026-08-19 (user-sanctioned): 41 stale pre-rename/Wren-era
   binaries pruned from Bin/Debug/Linux64-Clang-ASAN; 70 current entries
   kept.
4. RESOLVED: the user verified the MSVC fix on Windows directly - no
   Windows session needed.
5. The AngelScript delegate-funcdef cleanup seeded for Fable (1cedccbd) is
   acknowledged as next-week work, not reviewed here.

Baseline for pass 13: a7b951f7 + this pass's doc commit.

## Review pass 13 (2026-08-23, Fable): heightfield chain + terrain foundation/Phase A + HiDPI + theme page + math reflection - PASS

Scope: 85 commits since pass 12 (a7b951f7..6138a9f4 + review-day work). A
large share was Fable-built under in-line review this session
(scene-composition adoption + FrameTime cutover, messaging COMPLETE,
networking extraction COMPLETE, script-surface CLOSED, Guid string ctor,
PIE cook gate, one-app-keyboard arbitration, defaultapp facade root,
mesh-lod P0-P3 + skinned + overlay, Foundation::Lod extraction); the
deep-review targets were the Opus chunks. Full two-compiler battery
ALL_GREEN (155 test binaries each); ASAN sweep over the lifetime-
sensitive change class (Physics/Engine.Physics/Heightfield x3/Terrain x3/
Engine.Terrain/Geometry.Pipeline/ModelImporter/Render/Lod) - clean.

**Verified against the rulings trail:**
- Heightfield chain: the corrected NO-HAND-PADDING Jolt rule is honored
  verbatim (BuildHeightfield passes the grid straight through, comment
  cites Jolt's internal block rounding + cNoCollisionValue; the test pins
  the outside-extent miss). Size contract exposed as IsValidSize/
  NextValidSize; ENFORCEMENT correctly lives at the factory (Build
  validates size + blob length -> empty grid) and the cook (defensive
  snap) rather than the runtime ctor. Sidecar rule on the "heights"
  stream (ImageResource precedent named). ShapeKind::Heightfield APPENDED
  (wire values stable); RigidBody/Collider v2 gated + DataVersion(2)
  both; u16->f32 conversion buffers have stable lifetimes (inner-Array
  heap pointers survive outer growth; Jolt copies in-scope; ASAN
  agrees). Ref-picker dispatch for Ref<Heightfield> AND
  Ref<TerrainResource> present. Editor page = 2D-by-asset-identity per
  the ruling. Implicit Asset::fileName recipe hashing covers the
  heightmap source (checked - no missing dependency).
- Terrain foundation: chunk grid/quadtree/selection pure + tested;
  coverage selection delegates to Foundation::Lod (ONE formula - the
  layering ruling adopted end-to-end); the Y-rotation-preserves-radius
  transform note is correct. Terrain -> TerrainResource rename matches
  naming convention. engine.terrain Phase A: FullSceneComposition
  ModuleCount 9 -> 10 WITH the tripwire test updated (fifth tripwire
  save), component checklist complete (displayName/category/
  DataVersion(1)/picker), no per-frame loops yet so no active-gating
  obligation - the renderer phase inherits that rule.
- HiDPI Px->Dp (65 sites/27 files): mechanical + uniform; user-verified
  on-screen at 1.75. UIThemePage: Editor.GameUI placement + UI.Pipeline
  stays Toolkit-free (pipeline-ui-free rule checked). Math reflection
  ops: the one same-arity overload pair carries OverloadedName
  (Mul/MulScalar - the contract's pattern); natural types throughout.

**Findings:**
1. FIXED (this pass): HeightfieldAssetBuilder snapped invalid SIZES but
   not degenerate EXTENTS - a hand-edited worldSize=0 or maxY<=minY asset
   cooked a grid that NaNs world<->grid math and hands Jolt zero scales.
   Now snapped in the builder's own defensive pattern (kMinSpan floor on
   the footprint + an open Y range) with a cook regression test through
   the factory path.
2. Note, no action: the Heightfield runtime ctor trusts its inputs by
   design; both production paths (factory + cook) validate. Code-built
   grids are the author's contract.
3. Note: product reads in tests should go through the factory
   (CookAndBind) like the suite's siblings - the raw ReadObject path is
   not the production shape (cost this pass one test-fixture rewrite).

Baseline for pass 14: this pass's commit.

## Review pass 14 (2026-08-23, Fable): the terrain renderer arc - PASS (3 findings, all fixed in-pass)

Scope: 17 commits (38fabd67..28d0fc65) - the whole terrain renderer:
shared grid mesh, GPU height-texture cache, CPU extract, the chunked
geo-mipmap renderer (Phase C+D1), DefaultApp wiring, pixel probes
(Vulkan + desktop WebGPU + DX12-gated), sun wiring + back-face cull,
LOD-seam skirts, GBuffer output, in-memory/live-edit enablers, and the
TerrainPlayground sample. Full two-compiler battery green; ASAN over the
terrain/render/heightfield class clean after the fixes below.

**Verified:**
- Architecture is the ruled shape: dynamic-category renderer via
  RegisterRenderer (Engine.Render stays terrain-free), per-view cull+LOD
  in Resolve REPLAYING foundation.terrain's tested ExtractVisibleChunkDraws
  (one CPU path, no formula #2), shared grid VB + per-LOD IBs + per-chunk
  dynamic uniforms (no instancing, so the SV_InstanceID rule is moot).
- The WebGPU-strictness rule caught a REAL bug before it shipped: the
  4-target GBuffer PSO mismatch WebGPU rejects outright while Vulkan
  silently fed garbage to the SSR/TAA/motion targets - and the probe now
  requires WebGPU to be PIXEL-EXACT against Vulkan (identical
  filled/total counts), proving the WGSL cook of the R16Uint Load path.
- AO/SSR correctness checked: terrain writes all four GBuffer targets +
  its depth in the opaque pass, and AO/SSR consume OPAQUE depth+normals,
  so terrain is fully visible to them. Skipping the depth prepass costs
  terrain only early-Z (perf note, not a gap; a ResolveDepthOnly can
  come with the CSM-cast work it is listed alongside).
- ExtractRenderData gates on IsEffectivelyActive (the new-loop rule);
  TerrainComponent v2 (lodBias) wire-gated + reflection DataVersion(2);
  skirts ship with a diagnostic toggle the probe uses to prove cracks
  leak without them; the probe also proves the sun DRIVES shading
  (flipping it inverts the asymmetry).

**Findings (all fixed in-pass):**
1. The height-texture cache keyed entries by RAW Heightfield POINTER
   while its own comment claimed id+version - the exact aliasing class
   the bind-group-cache rule forbids (fresh grids all start at version
   1; a dead grid's reused address serves the dead grid's texture).
   FIXED: Heightfield gains `uid` (the StaticMesh precedent, comment and
   all), the cache keys by uid, and a regression test pins the
   dead-A/fresh-B-same-version scenario.
2. The chunk-model cache in TerrainComponentManager: the same pointer
   keying - FIXED with the same uid key.
3. The renderer tests leaked their caches' GPU objects (no
   Clear(device) before scope exit) - the ASAN battery caught it;
   FIXED in the tests (the production shutdown path was already
   correct).
Note, no action: cache entries for dead heightfields persist until
shutdown (bounded by the number of heightfields ever seen; benign today,
an eviction hook can ride the sculpt work if profiles ever care).

Remaining for terrain (per Opus, confirmed accurate): CSM cast+receive,
D2 splat, Editor.Terrain phase 2. Deferred set unchanged.

Baseline for pass 15: this pass's commit.
