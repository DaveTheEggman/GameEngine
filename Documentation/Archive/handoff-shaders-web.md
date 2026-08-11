# Handoff: shaders + web track (state + follow-ups)

Originally written 2026-07-30 as a continuation brief. Updated 2026-07-31: the entire
"what to do next" list below has SHIPPED, plus the browser milestone itself. This file is
now a state-of-the-track record + follow-up list, not a to-do handoff.

Baseline commit for the pending REVIEW is still **14f16a84** ("Materials: test expectations
for 16-byte uniform rounding"); everything after it is the continuation work.

## Review status

**Fable runs the code review of `14f16a84..HEAD` on Saturday (2026-08-02).** One finding is
already in and fixed ahead of it: the web track was developed clang-first and a gcc-only
"interface partition is not exported" break had slipped in (`WebGpuModule.cppm` never
`export import`ed `:push_constant_emulator`) - fixed in `333f3568`. Because that was
clang-first drift, the review should sweep the span specifically for more of the same
(gcc-only module hygiene, both-compiler greenness, tests-present, RHI-vs-renderer commit
isolation, no Co-Authored-By, ASCII hyphens).

## Where the two tracks stand NOW (2026-07-31)

**Shaders track (#113):** P1 (source providers + hot reload) DONE at the baseline. P2
(cook-time bytecode) and P3 (SPIR-V -> WGSL) DONE since:
- `af81943e` WgslTranslator (cook-time HLSL -> WGSL); `ae835fc1` vendored naga-cli + tint
  (ThirdParty/Naga, ThirdParty/Tint); `2b920838` WGSL uniformity fixes.
- `3d1303e0` variant model (declared masks, canonicalization, power-set, drift-lint);
  `ebd3c893`+`de342482` CookedShaderPack; `2d11f394`+`ca1f019e` dist consumes the pack and
  drops the DXC sidecar; `fdc73424` standalone ShaderPackCook CLI.
- `6486dd9f` PUSH_CONSTANT portability macro; VG + ImGui migrated off inline shaders
  (`5ec102d1`, `df058480`, `67ae507e`, `fce8cd66`, `6501d24d`) via the new ShaderSystemHost.

**Web track (#112):** the whole runtime climbed to wasm (`69a652c6..b4ebad8c`), the WebGPU
backend compiles for the browser (`c88e1e6a` emdawnwebgpu port; `397e2fb4` uniform push-
constant fallback), and the BROWSER MILESTONE landed: `b64ec425` WebTriangle, then
`5d9a97c7`->`0cfee81d` the full `DefaultApplication` 3D scene (spinning PBR cube + ground +
light + FlyCamera) rendering in a browser on the cooked WGSL pack, user-verified in
Chromium/Opera. HTML5 input (`baa3db31` kbd/mouse, `16ec0a6a` touch/gamepad), full-window
canvas (`95ff2d65`), preferred-format config (`06614248`), wasm-shipping preset (`0d20b7cc`,
~5.9MB wasm).

**Web frame-perf pass (2026-07-31, post-milestone):** two boundary-crossing costs killed.
`c4b23fb8` skips byte-identical persistent-shadow re-uploads (writeBuffer 59% -> 0%);
`1f2e3fff` makes the fence wait yield to the browser (event pump 67% -> 0%, frame now idle-
bound at the rAF cap). Details + the deferred memcmp item live in the `web-platform-track`
memory. The temporary `[frame]` timing probe in `WebRunner.cppm` is intentionally
UNCOMMITTED (kept for measuring; revert before a real ship).

## Pending / follow-up items

1. **DONE 2026-07-31 (commit `9956a69a`): AngelScript on web.** Turned out to be almost pure
   un-gating - as_config.h auto-enables AS_MAX_PORTABILITY + AS_NO_THREADS on `__EMSCRIPTEN__`
   (native-call trampolines compile out) and the backend was already all-generic (every binding
   via asCALL_GENERIC through the `*Dispatch` fns). Changes: ThirdParty builds draconic_angelscript
   for Emscripten (Threads linked only off-web); DefaultApp links ScriptAngelScript + defines
   DRACONIC_HAS_ANGELSCRIPT UNCONDITIONALLY (mirrors Wren - a TARGET guard failed because DefaultApp
   configures BEFORE the Script subdir on the web branch; forward-referenced link names resolve at
   generate time, immediate if(TARGET) does not). Backend subdir + tests auto-climb via the existing
   `if(TARGET Draconic::AngelScript)` guard. Verified: node tests 24/209, desktop 209/209 clang+gcc,
   WebScene links AngelScript (82M->93M). REMAINING SMOKE (user): run an AngelScript-authored game
   script in a browser to confirm end-to-end + the SetLineCallback/CDECL debugger path (only
   SetMessageCallback/CDECL was headless-verified; both are host callbacks, not trampolined).
2. **DONE 2026-07-31 (commits 6a4ae071, a9cd6a50): web player + script-backend toggles.**
   DraconicPlayer (the generic game runner, Code/Tools/Player) now has a BROWSER entry: PlayerApplication
   extracted to a shared header (the DRACONIC_APP_MAIN classic-header pattern), desktop Main.cpp
   unchanged, new WebMain.cpp = DRACONIC_APP_MAIN(WebPlayerApplication) with projectDir "." (no argv;
   dist preloaded). CMake EMSCRIPTEN branch links the web trio, cooks + preloads engine shaders.dpak
   (SEPARATE from the dist - dists carry only project assets), preloads the dist's Content.pak +
   player.xml (DRACONIC_WEB_PLAYER_DIST, defaults to the staged EditorProject dist), own shell.html
   (<title>Draconic Player</title>). Emits DraconicPlayer.html/.wasm(94M)/.data(643K). Also: Wren gating is
   now SYMMETRIC with AngelScript - DRACONIC_ENABLE_WREN / _ANGELSCRIPT options (default ON), the run
   resolves the backend by the game script's LANGUAGE so either drops cleanly. AWAITING user browser
   smoke (does the EditorProject dist render + run its script; it is a Jul-29 dist, may need re-export).
3. **ACTIVE-NEXT: Web networking backend (WebSocket transport).** The transport seam
   `INetTransport` (NetTransport.cppm: Send/Disconnect/Poll/Update/Stats, event-driven, time via
   Update) was BUILT for this - the module comment names "later websocket/webrtc" explicitly, and
   Net already COMPILES for wasm (only the raw-socket TESTS are desktop-only). Desktop rides
   reliable-UDP over raw UDP/TCP; browsers have NO raw UDP, so web needs a WebSocketTransport
   implementing INetTransport via emscripten/websocket.h (onopen/onmessage/onclose callbacks ->
   enqueue, drained by Poll; binary frames preserve datagram boundaries; WebSocket is reliable+
   ordered, so ReliableOrdered maps 1:1 and the unreliable channels degrade to reliable - fine for
   the turn-based/RTS primary channel). ARCHITECTURAL CONSTRAINT: a browser can only CONNECT, never
   listen/accept -> a browser peer is CLIENT-ONLY; hosting/dedicated-server stays native. OPEN
   QUESTION for the user: does the native server also grow a WebSocket-accept transport so browser
   clients join a Draconic server directly, or go through a relay? Testing is harder than desktop
   (socket tests can't run headless under node - needs a ws proxy/server); lean on an integration
   test or a native ws-server loopback.
4. **Firefox WebGPU compat.** Chromium/Opera render the scene; Firefox goes black + throws
   "render bundle incompatible read-only flags" though the pass/bundle depth+stencil flags
   match exactly (proven by instrumentation). Suspected Firefox wgpu-based-WebGPU strictness/
   maturity, not a real mismatch. Separate compat item.
5. **DONE 2026-08-01 (Fable): P4 Web export pipeline.** The web player now FETCHES player.xml + Content.pak + shaders.dpak from the serving folder at startup (ASYNCIFY wget; nothing baked at link time), which makes the wasm build a reusable EXPORT TEMPLATE. `--template create Bin/Debug/Emscripten-Clang --install` synthesizes + installs it (CreateTemplate grew a web-dir branch: platform "Web", player = the .html, sidecars from the build-emitted runtime-libs manifest incl. serve.py). Export with a platform-"Web" preset stages page + sidecars + Content.pak + player.xml + the WGSL shaders.dpak (FormatsForPlatform already mapped Web->WGSL) into a folder that is directly servable: `python3 serve.py` (COOP/COEP + wasm MIME + no-store) and browse Draconic.Engine.Player.html. Integration-tested end-to-end (ExportTests: Web preset -> staged page/sidecars/WGSL pack). Template 'draconic-web-debug-0.1.0' is installed on this machine. Awaiting the user's browser smoke. (original item follows)
   **(historical) P4 "Web" export preset.** The web PLAYER now exists (item 2) and preloads a dist via
   --preload-file, which bakes ONE dist at link time. The open export-side piece: deliver an
   ARBITRARY exported project's Content.pak to the PREBUILT web player without relinking - emscripten
   file_packager (a separate Content.data) or a runtime fetch of Content.pak - plus a formal Web
   export preset that stages DraconicPlayer.{html,js,wasm} + the generated data. Export already
   auto-cooks shaders.dpak (`ca1f019e`).
6. **memcmp headroom (deferred, user-decided 2026-07-31).** The shadow-skip's compare is
   ~1ms/frame; app is idle-bound so it's headroom. Real fix needs an explicit write-hook on
   the persistent-map path. Revisit only if a heavier scene goes CPU-bound. See memory.

## Browser render fixes + the TAA-off flip (2026-07-31)

Getting the exported EditorProject to render CORRECTLY in a browser (not just boot) surfaced a
cluster of WebGPU-vs-Vulkan issues. Root theme: **the engine was built around Vulkan's negative-
height viewport (which flips clip-space Y for every pass); WebGPU's setViewport cannot take a
negative height, so screen-space RECONSTRUCTION and OFFSCREEN fullscreen passes come out Y-flipped
on WebGPU.** New `rhi::Device::NeedsClipSpaceYFlip()` (WebGPU=true, Vulkan/DX12/Null=false) drives
the per-effect compensations.

SHIPPED (all verified on-screen in Chromium + desktop `--webgpu`):
- **Sky / shadows / SSAO Y-flips**: sky ray sign (`SkyFlags.x`), shadow sample uv.y sign
  (`ShadowParams.y`), SSAO AO-buffer sample flip (`ApplyPushC.flipAoY`) - each keyed off
  NeedsClipSpaceYFlip. Ground-plane cull was a separate earlier front-face-winding fix (committed
  `a00a0eba`).
- **Web console logging**: `WebMain`'s `DRACONIC_APP_MAIN` web body never registered a `ConsoleSink`,
  so ALL `GlobalLogger` output was invisible in the browser (only `rhi::LogInfof`/stdout showed).
  Registered one in the web macro body (`AppMain.h`). Dawn validation errors already print via the
  device uncaptured-error handler as `[webgpu] uncaptured error ...`.

**Black sky - how it was worked around.** The IBL env cube is a ONE-SHOT bake. On web the swapchain
hands out a SINGLE surface texture reused every frame and `wgpuQueueSubmit` validates ASYNCHRONOUSLY;
with the default 2 frames-in-flight the next frame's AcquireNextImage releases that surface texture
while the prior frame's still-pending submit references it -> Dawn drops that submit
(`Destroyed texture ... used in a submit`), and the one-shot bake rides in the dropped submit -> the
env cube stays black -> black sky. Geometry survives because it re-renders every frame; only the
one-shot bake is lost. THREE changes fix/work around it:
  1. **Single frame-in-flight on web** (`Graphics.cppm FromBackend`, `#if DRACONIC_PLATFORM_WEB`) - so
     BeginFrame's fence wait guarantees a frame's submit completed before the next acquire releases
     the surface texture. (Also gave a small fps win - the fence wait yields cleanly.)
  2. **Defer the web surface-texture release** from `Present` to the next AcquireNextImage.
  3. **Env-bake warmup** (`IBLSystem::Context::m_bakeWarmup=20`) - re-bake the cube for the first
     frames so it lands on a submit that survives, instead of being lost forever to one bad frame.
  The `Destroyed texture` error is REDUCED but NOT fully eliminated (it fires ~once at startup, not
  every frame - it's a deep emdawnwebgpu submit/present timing thing where the browser validates the
  submit only AFTER the frame presented and the surface texture expired). It does not block
  rendering. Proper fix later: give the IBL precompute its OWN submit that never targets the
  backbuffer, or restructure the web present so submits flush within-frame.

**FIXED 2026-08-01 (Fable): whole-image flip on WebGPU when TAA is OFF.** The TAA resolve's NDC-based uv reconstruction was the pass un-mirroring the scene on Y-flip backends; with TAA off the mirrored scene reached the screen uncorrected. Tonemap now un-mirrors instead when TAA didn't run (FlipSceneY push, ANDed with NeedsClipSpaceYFlip - Vulkan untouched), so the LDR intermediate, post-tonemap UI, and FXAA all operate upright. Awaiting the user's four-combo visual check (TAA x FXAA on webgpu). Original notes kept below for history.

**(historical) KNOWN RESIDUAL - whole-image flip on WebGPU when TAA is OFF.** User-confirmed: TAA off -> the entire
image is upside-down on WebGPU (desktop `--webgpu` AND browser); TAA on -> upright. Cause: TAA-off
routes tonemap -> LDR **offscreen** intermediate -> FXAA -> backbuffer, and a fullscreen pass
rendering into an OFFSCREEN texture is Y-flipped on WebGPU (same root as the sky/shadow/AO flips - no
negative viewport). TAA-on writes straight through, so it stays upright. The per-effect fixes patched
three consumers; the FXAA/no-TAA final path is another manifestation. **Workaround: keep TAA on.**
PROPER FIX (TODO): systematically compensate the offscreen fullscreen Y-flip on WebGPU for the post
passes - audit FXAA, TAA-resolve, bloom-composite, AO-apply outputs; ideally one shared fullscreen-VS
convention gated on NeedsClipSpaceYFlip rather than per-pass patches. Reproduces on desktop
`--webgpu`, so it's debuggable without a wasm rebuild.

## Web packaging (current workflow, 2026-07-31)

How the browser build is produced RIGHT NOW (there is still NO "Web" export preset from the
editor - item 5 - so this is the manual flow):

- **Binary:** `Draconic.Engine.Player` (its `WebMain.cpp`), built with
  `cmake --build build/wasm --target Draconic.Engine.Player`. Outputs
  `Bin/Debug/Emscripten-Clang/Draconic.Engine.Player.{html,js,wasm,data}`. The `.data` is the
  emscripten file_packager MEMFS bundle.
- **What gets baked into `.data`** (see `Code/Draconic/Engine/Draconic.Engine.Player/CMakeLists.txt`):
  1. `shaders.dpak` - a custom target (`Draconic.Engine.Player.ShaderPack`) cooks WGSL from
     `Data/Shaders` with the native `Draconic.Tools.ShaderPack` into
     `build/wasm/.../Draconic.Engine.Player/shaders.dpak`, preloaded at `/shaders.dpak`. These are
     the ENGINE shaders, cooked fresh from source - SEPARATE from any dist.
  2. `Content.pak` + `player.xml` - preloaded from `DRACONIC_WEB_PLAYER_DIST` (a CMakeCache PATH,
     defaults to `Bin/Debug/Linux64-Clang/EditorProject/Dist/Linux64`).
- **Content source = a DESKTOP export**, not a web export: `./Bin/Debug/Linux64-Clang/Draconic.Tools.Export <projectDir>`
  regenerates that Linux64 dist's `Content.pak`/`player.xml`. The wasm player just reuses it.
- **GOTCHA that bit us (2026-07-31):** emscripten `--preload-file` inputs are NOT tracked as ninja
  link dependencies. After re-exporting the desktop dist (Content.pak/player.xml change), the
  `.data` does NOT rebuild - the browser keeps serving the OLD scene. The shaders.dpak custom
  target DOES re-cook on a `Data/Shaders` edit, but the relink/repackage that folds it (and the new
  Content.pak) into `.data` still won't fire. Force it:
  `rm Bin/Debug/Emscripten-Clang/Draconic.Engine.Player.{data,html,js,wasm}` then rebuild the target.
  Always confirm by timestamp that `.data` is NEWER than the dist `Content.pak`.
- **Serve** with COOP/COEP (SharedArrayBuffer - the ASYNCIFY/threads build needs it) + wasm MIME.
  `dotnet serve -d Bin/Debug/Emscripten-Clang -p 8000 -h "Cross-Origin-Opener-Policy: same-origin"
  -h "Cross-Origin-Embedder-Policy: require-corp"`. Add `-h "Cache-Control: no-store"` - the browser
  caches the ~141MB `.data` HARD, and a plain Ctrl+Shift+R often still serves the stale bundle
  (verify in DevTools Network that the `.data` request is 200 + the expected size, not disk-cache).
- **Rebuild scope:** for a browser test only the wasm player + its shaders.dpak matter. The desktop
  editor (`Draconic.Tools.Editor`) needs a rebuild only to EDIT the project, never to package.
- **Harness caveat (learned debugging the black sky):** the desktop `--webgpu` +
  `DRACONIC_WEBGPU_WGSL=1` harness reproduces the browser's WGSL SHADERS but runs on wgpu-native,
  NOT Dawn - so it does NOT reproduce device-level differences (feature sets, format/usage
  strictness, transient lifetime). Browser-only bugs still need the browser + its console (the
  WebGPU device already logs Dawn validation failures as `[webgpu] uncaptured error ...`, and
  `rhi::LogInfof` prints to the console).

## Key seams and facts (reference)

- `Shaders/System/ShaderSystem.cppm`: GetVariant(name, stage, flags) compile-on-demand
  cache; provider fetch on source miss; explicit RegisterSource wins; `Version(name)`
  monotonic bump per InvalidateShader (everything rebuilds by polling it). `PumpReloads()`.
- `ShaderSystemHost` (Shaders/System): the ONE place that turns a device into a ready
  ShaderSystem (cooked-pack-vs-dev-DXC decision). RenderSubsystem + VG/UI/ImGui all use it.
- `FileShaderSourceProvider`: NativeFileSystem mount, recursive stat-sweep, throttled
  1-in-60 PollChanges; a `.hlsli` change reports ALL names (includers unknown).
- WebGPU compile path: `opts.spirvTargetEnvironment = "vulkan1.1"` when device is WebGPU
  (naga rejects SPIR-V 1.4+ / OpCopyLogical). DX12 = DXIL, else SPIR-V. Device-reported
  shader format selects the cooked blob (`209f4eda`).
- Cook: WgslTranslator shells naga + tint (cook-time only, never linked); ShaderPackCooker
  builds the per-backend/per-variant CookedShaderPack; `DraconicShaderPackCook` is the CLI.
- Binding shifts engine-wide: CBV=0 / SRV=+100 / UAV=+200 / Sampler=+300 (WebGPU caps
  binding index at 1000 in browsers). Push constants: native Immediates on wgpu-native;
  uniform-buffer fallback at @group(space)@binding(0) on the browser.
- Material uniforms round to 16 bytes (WebGPU validates bound range vs the padded cbuffer).

## Gotchas that will bite you

- **Occluded/unfocused Wayland windows are present-throttled**: the frame loop + reload pump
  stall, so hot reload / rendering can look broken when it is not. Test with `--null-gpu`
  (never blocks) or a visible focused window. (Suspected cause of the residual linux-vs-
  windows lag too.)
- **Web fence/async waits MUST yield** (`WebGpuApi::Yield()` = emscripten_sleep(0) on web):
  the browser resolves work-done/map futures from a microtask that cannot run while wasm
  spins. All three pump loops (PumpUntil, PumpUntilWithDevice, WebGpuFence::Wait) route
  through it. Do not remove the yield. ASYNCIFY must be on (and ASYNCIFY_STACK_SIZE=1MB for
  the deep render-frame pump - the 4KB default overflows -> "unreachable" trap).
- wgpu-native v29 PANICS on `wgpuInstanceWaitAny` timeout>0 and `wgpuGetInstanceFeatures`.
  Fences use submission-index waits; pumps are non-blocking polls + ProcessEvents. Do NOT
  "fix" that back to WaitAny.
- `depthClearValue` must always be FINITE on web (the wgpu INIT sentinel is NaN; the browser
  rejects NaN even on read-only/Load depth). Bundle depth/stencil readOnly flags must MATCH
  the pass exactly (Chromium tolerant, Firefox strict - see follow-up #2).
- GCC module interface hygiene: heavy third-party headers + DRACONIC_REFLECT_* bodies go in
  IMPLEMENTATION units, never interface units. Every interface partition must be
  `export import`ed by the primary module unit (clang tolerates a gap, gcc does not - this
  bit us at `333f3568`).
- Bind-group caches invalidate by version/generation, never raw pointer.

## House rules (non-negotiable)

- Debug builds, both compilers, `-j4` (OOM otherwise): `cmake --build build/clang -j4` and
  `build/gcc`. Both must be green before claiming done.
- Every addition lands with doctest tests in the module's Tests/. Run the affected suites AND
  `ctest` in build/clang.
- Commit only when asked, in logical pieces; RHI commits ISOLATED from renderer commits; NO
  Co-Authored-By trailer; ASCII hyphens only (no em/en-dashes) in code/comments/docs.
- docs/ is intentionally untracked - update files there but never `git add docs/`.
- Never delete Bin/ output (the user's EditorProject lives inside).
- Full descriptive names, PascalCase methods, UTF-8 (char8_t) string currency.
- The user does all visual verification; hand them Debug binaries and say exactly what to
  look at. Run structural decisions by the user BEFORE implementing.

## Known pre-existing failures (not yours, do not chase unless asked)

- `DraconicGUITests` segfault (parked draconic.gui module; unrelated to shaders/web).

## Verification recipes

- Renderer smoke: `timeout -k 3 12 ./Bin/Debug/Linux64-Clang/Sandbox --vulkan` (and
  `--webgpu`) then grep the log for validation errors / "compile failed" / "not found".
- Web build: `cmake --build build/wasm --target WebScene`; serve `Bin/Debug/Emscripten-
  Clang/` over http (localhost = secure context for navigator.gpu) and open WebScene.html.
- Shader/RHI tests: `./Bin/Debug/Linux64-{Clang,GCC}/DraconicShaderSystemTests`,
  `DraconicRenderTests`, `DraconicMaterialsTests`, `DraconicRHIWebGPUTests`.
