# Review: 14f16a84..HEAD (96 commits - shaders P2/P3, web-to-browser, reorg, render/VG fixes)

Reviewed 2026-08-01 by Fable (5 parallel area reviews + mechanical sweeps + live checks;
headline findings independently re-verified against committed code). Baseline health at
HEAD: clang + gcc build green, full ctest green except the PRE-EXISTING Draconic.GUI.Tests
segfault, wasm preset configures and builds Draconic.Core on Linux.

Working-tree note: findings refer to COMMITTED state only. Anyone fixing these while VG
work is in flight: commit fixes by explicit path only.

## ACTIONED 2026-08-01 (Fable)

Fix commits (each build-verified clang+gcc, suites green, smoked where applicable):

- `df21a8d0` Render: shadowParams.y hole (majors 1), sky flip in NDC space (major 2),
  debug_geom bindGroupIndex=0 (major 3), Y-flip comment truth (nit).
- `ed358fc6` Shaders cook/pack: include-expanded drift-lint (major 7), lint comment
  false-positives, SPIR-V cook target vulkan1.1 (NEW finding: a vulkan1.3 pack panics
  wgpu-native - reproduced live, now asserted by test), tint hard-fail policy (major 8),
  pack Read count-guards + Add dedup + deterministic file order, ThirdParty em-dashes.
- `66f1f19e` Shaders system/host: dev-first policy + ShaderPackPolicy + DRACONIC_USE_
  SHADER_PACK opt-in (major 4 - hot reload verified alive with a pack present),
  registered-source-beats-pack (major 5 - user shader assets resolve in pack mode when a
  compiler exists; compiler-free dist logs once, honest gap until user shaders cook),
  dev canonicalization for corpus sources (major 6), cooked-miss log throttling.
- `b506a51a` RHI.WebGPU: heap+orphan records for the four remaining stack-captured async
  callbacks (major 9), maxPushConstantSize always advertised, emulator Begin clears the
  shadow, caps-leak branch; NEW tests: render-pass emulation at group 0 with two-draw
  no-aliasing proof, fence-destroyed-with-pending-callback regression.
- `a138353c` Shell.Web/Runtime.Web: probe removed + original dt restored (major 10),
  preventDefault passthrough for browser chords/F-keys (major 11), HiDPI pointer scale
  (major 12), wheel deltaMode (a concrete Firefox-compat cause), missing key mappings,
  gamepad compact-index events.
- `900570ed` Build: web samples honor DRACONIC_BUILD_SAMPLES (major 13; dev wasm preset
  sets samples ON - WebScene stays the browser-test workflow), GUI/Imgui sample option
  gating, Player web cook VERBATIM + CONFIGURE_DEPENDS + configure-time existence
  checks, ThirdParty web-gating comment.
- `3f27d38a` VG: THE BLOCKER - persistent content-keyed gradient-LUT cache + the
  VGBatch::evictedTextures renderer eviction protocol with frames-in-flight retirement;
  per-FillPath rebake gone; full context+renderer test coverage.

Also reviewed post-snapshot: f1090a1f + 40520e81 (Opus's per-pixel gradient shaders +
host wiring) - clean, degradation-gated, and the full 62-file corpus WGSL-cooks under
the now-mandatory tint gate.

Verified at the new HEAD: clang+gcc full builds, full ctest (only the pre-existing
Draconic.GUI.Tests segfault), Sandbox vulkan+webgpu clean in dev mode AND in opt-in
pack mode (fresh vulkan1.1 pack), wasm configure+WebScene link, VG/UI sandboxes clean.
A regenerable cooked pack sits in Bin/Debug/Linux64-Clang (harmless now - dev-first).

Deliberately DEFERRED (minors; unchanged from the lists below): ShaderSystemHost
x4-per-process duplication; WgslTranslator scratch cleanup/collision; RunProcess Win32
A-API/quoting/argv edges + EINTR; Jolt patch as a vendored .patch; JobSystem web
workerCount explicitness; IBL warmup gating; web TextInput; PumpUntil device-lost
early-out; memcmp-skip GPU-write caveat; 13 legacy test names; dead transitional
include root; 8-flag variant cap assert; web swapchain sRGB forcing (browser-verified
behavior, left as is); emulator layout-handle AddRef; zero-size Configure bookkeeping.
Plus the two documented residuals owned by the web track: TAA-off offscreen flip and
the remaining Firefox items.

## Verdict

The span is impressive and mostly well-built: the browser milestone is real, the reorg is
near-perfect (328/328 targets and 100/100 test registrations mapped, folder==target==module
holds tree-wide), the WebGPU bring-up decisions are sound, and the cook pipeline's wire
format, power-set enumeration, and loud-miss behavior are correct. But the shaders arc
half-keeps its two central design promises (canonicalization and the drift-lint), the
pack-vs-dev policy silently kills the P1 hot-reload payoff, and a handful of real
regressions (spot/point shadows, VG gradient cache, sky flip space) need fixing before the
arcs are "done".

## Blocker

- **VG gradient-LUT vs raw-pointer texture cache** - `Draconic.VG/Context.cppm` frees the
  per-frame LUT `OwnedImageData` on every `Clear()`, while `Draconic.VG.Renderer/
  Renderer.cppm` caches GPU textures keyed by raw `ImageData*`, uploads only at first
  sight, and never evicts (comment at ~:312 admits it). Freed-address reuse -> a gradient
  renders LAST frame's ramp (stale/swapped colors on any animated/state-changed gradient);
  no reuse -> a leaked 256x1 GPU texture + bind groups per gradient per frame, unbounded.
  Textbook violation of the version/generation cache rule. Also: a LUT is baked per
  FillPath call, not per distinct gradient. THIS IS IN THE ACTIVE VG WORK AREA - route to
  the VG session. VERIFIED.

## Majors - correctness regressions

1. **Spot/point shadows break without a valid directional CSM (all backends).**
   `MeshRendererImpl.cpp:817` assigns `vd.shadowParams.y` (backend uv.y sign) only inside
   `if (ctx.cascades.valid)`; the struct default is `{40,0,0,0}` and forward.ps.hlsl's
   `SampleLocalShadow` multiplies by `ShadowParams.y` unconditionally -> uv.y == 0.5
   constant -> garbage shadow stripes in any scene with local shadow casters but no valid
   CSM. Pre-09aa the sign was hardcoded; this is a Vulkan regression inside a "Vulkan
   unchanged" commit. Fix: assign shadowParams.y outside the cascades.valid block. VERIFIED.
2. **Sky Y-flip applied in the wrong space (WebGPU).** `sky.vs.hlsl` flips the WORLD ray
   (`o.dir.y *= SkyFlags.x`) after unprojection; the capability's own doc (Device.cppm
   ~:155) prescribes negating NDC Y BEFORE unprojecting. Correct only for level cameras;
   any pitch/roll shears the sky vs geometry on flip backends. Follow-on: `sky.ps.hlsl`
   reprojects the flipped ray against the unflipped emitted NDC -> bogus whole-screen sky
   velocity -> TAA ghosting/smearing of sky + sun during camera motion on web. Fix: do the
   flip in NDC pre-unprojection so ray, env sample, and reprojection stay consistent.
3. **`debug_geom` dead on the web/WGSL path.** It is the ONLY shader with
   `PUSH_CONSTANT(..., space0)` while `PushConstantRange.bindGroupIndex` defaults to 1 and
   no caller overrides it -> under the browser push-constant emulation the synthesized
   uniform lands at group 1 but the WGSL expects group 0 -> pipeline creation fails
   validation, debug draw silently gone. Fix: move debug_geom to space1 like everything
   else (or set bindGroupIndex=0 at the call site); add a bindGroupIndex=0 test. VERIFIED.
4. **A stray cooked pack silently kills dev mode.** `ShaderSystemHost` prefers
   `shaders.dpak` (exe dir or cwd) over dev unconditionally; a stray pack in
   Bin/Debug/Linux64-Clang (present right now, dated 08-01) disables hot reload + live
   edits with one info line (renderer only; the UI/Imgui hosts don't even log). VERIFIED
   LIVE: the P1 break-a-shader test produces zero recompiles at HEAD. Recommend: dev-first
   when compiler+source root exist; pack-on-desktop testing via explicit opt-in
   (env/flag); also delete the stray Bin pack after confirming provenance.
5. **Pack mode has no fallback - user shader assets are dead in dist.** `GetVariant`
   short-circuits to the pack and never consults `m_sources`; `ShaderResource.cppm:101`
   still delivers project shaders via RegisterSource -> in a shipped dist/browser, every
   custom material shader hits the loud cook-coverage error and renders nothing. Inverts
   the stated "RegisterSource beats any provider" precedence. Latent today (EditorProject
   dist has no custom shaders) but collides with the locked custom-materials direction.
   VERIFIED. Fix direction: pack miss -> fall through to registered source (+compiler when
   present); longer-term, cook project shaders to bytecode too (P2 follow-up).
6. **Dev/dist canonicalization asymmetry.** `CanonicalizeFlags` has exactly one non-test
   call site - the cooked path. Dev compiles/caches RAW flags, so dev renders
   used-but-undeclared flags that dist strips, and the promised dev-side variant dedupe
   never happens. The design doc calls this exact mismatch a serious bug. VERIFIED.
7. **The drift-lint is blind to `.hlsli` includes** - which is where every real `#ifdef`
   lives (`forward.vs.hlsl` is literally directive + include; all 7 SKINNED/INSTANCED uses
   sit in `forward_vs.hlsli`). Undeclaring a used flag passes the lint and the dist
   silently loses the feature - the precise hole the lint exists to close, and #6 means
   dev hides it. Fix: lint the include-expanded source (the cook already reads files).
   VERIFIED.
8. **tint validation silently skipped on tint-less hosts.** `WgslTranslator.cppm:167`
   skips validation when tint isn't vendored (only win/mac/linux-x86_64 are), contradicting
   the class's own doc; a web export from such a host ships WGSL that Chrome/Dawn may
   reject at runtime. The CMake status message documents the skip, so intent is muddled -
   pick one: fail web exports without tint, or warn LOUDLY at cook time per file.
9. **Four more stack-captured async-callback records (same class as the fixed fence UAF).**
   requestAdapter/requestDevice (`WebGpuBackend.cppm:206-258`), `WaitIdle`
   (`WebGpuAdapter.cppm` area), `MapAsync` (`WebGpuBuffer.cppm:89-115`) capture stack
   locals and each pump has a give-up/timeout path that returns with the callback still
   registered -> a later ProcessEvents writes through a dead stack frame. fb7d7e8d fixed
   one instance of a five-instance pattern. Fix: heap-allocate + detach, like the fence fix.
10. **Committed frame-timing probe changes shipping behavior.** `WebRunner.cppm:62-101`:
    the self-labelled "Remove once diagnosed" probe (console line every 60 frames) rode
    into the tree via reorg commit 7be65c61 despite the handoff doc marking it
    intentionally-uncommitted - AND it changed dt semantics (dt now measured before
    ProcessEvents, not after). Ships in wasm-shipping. Remove (or gate) + restore/decide
    dt intent explicitly. This is also the reorg's only content smuggle.
11. **Web input: unconditional preventDefault on every key** (`WebInput.cppm:598` returns
    EM_TRUE always) - F5, F12, Ctrl+C/V, all browser chords dead while the app runs. Pass
    through Ctrl/Meta chords + F-keys.
12. **Web input: CSS-vs-backing-store coordinate mismatch on HiDPI** - pointer events are
    CSS pixels, window size is DPR-scaled backing pixels (capped 2x in shell.html) -> UI
    clicks land offset/half-scale at dpr>1.
13. **Web samples ignore DRACONic_BUILD_SAMPLES** (root CMakeLists ~:514: EMSCRIPTEN block
    adds WebTriangle/WebScene unconditionally + return()) - a "no samples" wasm build still
    builds both AND requires the native ShaderPack tool.

## Process / house-rules scorecard

- Commit hygiene: GOOD. No Co-Authored-By anywhere; RHI-vs-renderer isolation held (one
  defensible atomic cross-layer browser-fix commit, 0cfee81d); reorg commits honestly
  declared their non-move content (except the probe, above).
- Em-dashes: two in-span lines in ThirdParty/CMakeLists.txt (:114, :120) - fix when handy.
- docs/: emscripten-windows.md tracked INTENTIONALLY (user-confirmed exception: docs that
  must be portable with the repo). Rule memory updated.
- Both compilers: green at HEAD; 333f3568 was the only gcc-only module break and sweeps
  found no further unexported partitions. Hygiene deviations (build-safe, style): heavy
  headers in interface-unit GMFs - <windows.h> in WebGpuApi `:api`, emscripten headers in
  Shell.Web interfaces (em-clang-only, so no gcc risk; the Yield macro rename c84572da was
  a direct symptom).
- Tests: shaders CORE is well covered (variants/pack round-trip/lint/WGSL shifts/
  RunProcess). Gaps against the tests-required rule: ShaderSystemHost (zero tests),
  ShaderPackCook CLI, ImGui module (no test target), VG premult commit (no tests for a
  compositing change), no fence-UAF regression test, no render/bundle emulation or
  bindGroupIndex=0 tests, web input mapping tables untestable-by-construction (extract to
  a platform-neutral partition). Two red-window violations: e747d766 landed with
  Render.Tests red (fixed next commit); ca1f019e broke ExportTests until 59aa4c86.
- Reorg: EXCELLENT. Zero dropped targets/tests (one documented dead-module deletion),
  convention holds tree-wide, gates survived verbatim, baked data paths all valid.

## Minors worth a follow-up pass (grouped; see area notes for file:line)

- Shaders/pack robustness: pack Read trusts counts before validation (OOM on corrupt
  pack - violates count-guard rule); pack bytes not deterministic (collector never sorts
  despite comment); duplicate Add leaves orphaned double-serialized entries; variant
  enumeration silently caps at 8 flags (+ CombineKey collides past 13 bits); lint
  false-positives on comments containing "defined"/flag names, misses `#  if`; cooked-miss
  error spams per frame unthrottled; ShaderPackCook CLI leaks its scratch dir + collides
  on concurrent cooks.
- ShaderSystemHost duplication: four consumers each own a host -> dist loads shaders.dpak
  up to 4x, dev spins up to 4 DXC instances; VG/Imgui/UI hosts never pump reloads (their
  shaders don't hot-reload, unlike the renderer's); UIRuntime failure path says "loud" but
  logs nothing.
- WebGPU: emulated devices report maxPushConstantSize=0 (callers gating on it disable the
  very path the emulator serves) + stale "no-ops" comment; PushConstantEmulation snapshots
  the bind-group-layout handle without AddRef (works via wgpu internals, against the
  ownership spirit); memcmp skip desyncs if the GPU copy is written GPU-side
  (CopyDst is legal on those buffers); m_lastUploaded doubles host memory per CpuToGpu
  buffer; zero-size Configure records 0x0 w/h with the old config live; web swapchain
  FORCES the sRGB companion format even when non-sRGB was requested; caps-leak on the
  formatCount==0 branch; emulator Begin doesn't clear m_shadow (cross-pass stale push
  bytes); PumpUntil has no device-lost early-out (tab hangs ~minutes before the guard);
  Primary-backends restriction = WebGPU unavailable on GL-only machines (graceful, but a
  behavior change).
- Web input: PrintScreen/ScrollLock/Pause/NumLock unmapped; mods set combined L+R bits
  (desktop sets side-specific); no TextInput events (game-UI text fields dead in browser);
  gamepad event index vs compacted list mismatch (null deref risk); wheel ignores
  deltaMode (Firefox ~30x too slow - one concrete Firefox-compat cause); SetRelativeMode
  never requests pointer lock; callbacks never unregistered (safe only while WebShell is
  an immortal static).
- Build/CMake: Player cook lacks VERBATIM + configure-time existence checks (raw ninja
  errors on fresh hosts); shader GLOBs lack CONFIGURE_DEPENDS (stale dpak after adding a
  file); GUISandbox + 8 samples link GUI/Imgui without honoring the new
  EXPERIMENTAL_GUI/EXTENSION_IMGUI options (OFF + samples ON breaks configure); bare `-s`
  strip flag reaches em++ in wasm-shipping (fragile parse); 13 test suites keep legacy
  target names inside dotted folders (ctest -R by new names misses them); dead
  transitional include root at root CMakeLists ~:203; stale ThirdParty comment lists
  Jolt/AngelScript as web-gated-off.
- Core/Win32: RunProcess drops argv beyond ~62 silently (Linux) / truncates >8KiB command
  lines silently (Win32); backslash-run-before-embedded-quote still mis-encodes; A-APIs
  get UTF-8 (non-ASCII paths break - against the WideString-at-the-edge rule, but
  consistent with the file's pre-existing style); EINTR handling in drain/waitpid.
- Misc: Jolt <type_traits> fix is an in-place vendored edit with no .patch file (a
  re-vendor silently drops it); JobSystem on web relies on pthread_create failing
  (fire-and-forget Submit never runs; make workerCount=0 explicit on web); IBL warmup runs
  20 frames on every context on every backend incl. native (gate to web/flip backends);
  DX12 comments claim Y-flip=true while DxDevice returns false (one of them is wrong);
  VG shader source exists in two committed copies (module reference + Data/Shaders) -
  currently in sync, standing drift risk.

## Suggested routing

1. VG session (active): the LUT/texture-cache blocker + per-FillPath bake + premult tests.
2. Surgical fixes, safe now: shadowParams.y, debug_geom space, probe removal (+ dt
   decision), preventDefault filtering, em-dashes, stray Bin pack removal (after
   confirming provenance).
3. Design decisions needed from the user, then implement: pack-vs-dev priority, pack-miss
   fallback (and the dist story for project shaders), dev-side canonicalization, lint
   include-expansion, tint-missing policy for web exports.
4. Follow-up passes: WebGPU callback-record hardening + test gaps; web input polish
   (HiDPI, keys, TextInput, wheel); CMake robustness batch; pack robustness batch.
