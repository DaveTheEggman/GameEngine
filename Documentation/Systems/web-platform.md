# Web platform - Emscripten + WebGPU

Status: **decided (user, 2026-07-28).** The third of the three queued conversations (code
editor -> shaders -> Emscripten/WebGPU), held second because its conclusions feed the shaders
conversation.

Decisions:
- **Native-first bring-up via wgpu-native.** The WebGPU RHI backend is written against the
  standard `webgpu.h` C header and validated ON DESKTOP by loading wgpu-native (prebuilt
  shared library, same runtime-sidecar pattern as DXC - see dxc-runtime-sidecar). Full
  gdb/RenderDoc dev loop; the backend doubles as a bonus desktop backend; the Emscripten
  build is then a relink against the browser's implementation, not a port.
- **Single-threaded web v1.** wasm pthreads require SharedArrayBuffer, which requires
  COOP/COEP response headers from the HOST - a hosting constraint that rules out plain
  static hosts. The JobSystem gains an inline mode (0 workers; jobs execute at submit).
  Pthreads become a later opt-in for hosts that set the headers.
- **Backend now, browser after shaders.** P0 spike + WebGPU backend proceed immediately on
  native wgpu-native (existing runtime-DXC HLSL keeps working there via SPIR-V ingestion).
  The browser milestone lands once the shaders track delivers cook-time WGSL - web strictly
  requires it (no dlopen, no DXC, WebGPU accepts WGSL only).

## Current state - the engine is already web-shaped (deliberate; roadmap is WebGPU-first)

- `ApplicationHost` / `DesktopRunner` were DESIGNED for the split: both document "desktop
  blocks in a while-loop, whereas Emscripten must yield to the browser via a callback." A web
  runner is a second thin runner (`emscripten_set_main_loop`), not a refactor.
- The RHI is WebGPU-flavored by construction: `Commands.cppm` defines the bundle-safe subset
  as "exactly the subset valid inside a WebGPU render bundle"; `TextureFormat.cppm` follows
  WebGPU conventions; the renderer uses NO bind group past set 3 (the 4/4 budget = WebGPU's
  limit, on purpose).
- Renderer instancing avoids SV_InstanceID and bindless: StructuredBuffer + instance-stepped
  attributes - legal WebGPU (read-only storage buffers are allowed in vertex stage).
- Shell = SDL3 (first-class Emscripten backend). Audio = miniaudio (WebAudio backend). VFS
  has the scheme/capability seam a pak/fetch backend slots into. Export track already stages
  per-platform dists.

## Work items

1. **P0 - toolchain spike - PASSED 2026-07-29.** emcc + CMake + Ninja + C++23 modules
   WORKS: `draconic.core` + its full doctest suite compile to wasm32 and run green under
   node (215 cases / 8,073 assertions). Emscripten 6.0.5 ships `emscan-deps` and CMake
   wires the module-scanning pipeline automatically - `cmake --preset wasm` is all it
   takes (toolchain via `$env{EMSDK}`, host-neutral: works from Windows once
   emsdk_env.bat set EMSDK). The gate at the top of the root CMakeLists configures ONLY
   the wasm-proven layers; the engine climbs it incrementally. Findings the spike fixed:
   - `aligned_alloc(1, n)` returns NULL on emscripten (POSIX minimum is sizeof(void*);
     glibc is lenient) - SystemAllocator now clamps alignment up. Byte-aligned arrays
     (HashMap states) hit this immediately.
   - 32-bit `usize`: a `>> 32` in HashMap's NextPowerOfTwo was UB on wasm32 (rewritten
     as two legal shifts); expect more width assumptions as layers climb.
   - execinfo.h does not exist - `WriteBacktrace` is a web no-op (host provides traces).
   - Link defaults are demo-sized: `-sSTACK_SIZE=2097152 -sALLOW_MEMORY_GROWTH=1` set in
     the web gate (default 64KB stack overflowed immediately; desktop threads get 8MB).
   - Prelude gained DRACONIC_PLATFORM_WEB + DRACONIC_ARCH_WASM; Emscripten reuses the
     POSIX (Linux) System/Thread backends.
   - Guarded out of the web test run (only these): ThreadingTests (all cases SPAWN
     threads; the P3 JobSystem inline mode brings a subset back), the concurrent-log
     case, and the two dlopen plugin cases (web has no dynamic linking).
2. **P1 - `draconic.rhi.webgpu` - COMPLETE, USER-VERIFIED VISUALLY 2026-07-30.**
   The fourth backend (against webgpu.h, wgpu-native dlopen sidecar on desktop) runs
   the ENTIRE STACK: 26/30 RHI samples (4 graceful platform skips), the full Sandbox
   renderer (clustered PBR, shadows, IBL all sky modes, SSR, TAA, AO, decals,
   particles, split-screen), UISandbox (VG + fonts + UI), and DraconicEditor - all
   with zero validation errors, visually confirmed against Vulkan. Every executable
   picks its backend via graphics::SelectBackendFromArguments (--webgpu/--vulkan/
   --dx12/--null-gpu). Stage history + gotchas below.
   **STAGE 1 LANDED 2026-07-29 - device bring-up LIVE on hardware**: vendored
   wgpu-native v29.0.1.1 (ThirdParty/WgpuNative, headers + .so/.dll, DXC-style sidecar
   CMake), `draconic.rhi.webgpu` module (api/fence/queue/device/adapter/backend
   partitions), DeviceType::WebGPU + graphics BackendType::WebGPU + factory case.
   Real: dlsym function TABLE (desktop dlsym / web direct symbols behind one seam),
   instance + wgpuInstanceEnumerateAdapters, async RequestDevice via ProcessEvents pump,
   device-lost latch + uncaptured-error log, ONE WGPUQueue behind all three RHI queue
   types, CPU-side timeline-fence emulation over OnSubmittedWorkDone. Everything else
   returns honest NotSupported until its stage. Tests run LIVE on the sidecar + GPU and
   skip vacuously on machines without either (Vulkan/DX12 precedent).
   GOTCHA: wgpu-native v29 PANICS on wgpuInstanceWaitAny with timeout>0 ("not
   implemented") - all waits are AllowProcessEvents callbacks + ProcessEvents pump.
   **STAGE 2 LANDED (same day) - resources live on the RTX 2060**: conversions
   partition (near-1:1 format table; ClampToBorder narrows to ClampToEdge - no border
   sampling in WebGPU; RGBA16Unorm/Snorm unsupported -> honest NotSupported), buffers
   with the MAP EMULATION (RHI Map is Vulkan-persistent-shaped: CpuToGpu maps a CPU
   shadow + Unmap flushes via wgpuQueueWriteBuffer queue-ordered; GpuToCpu genuinely
   MapAsync(Read)+pump; WriteBuffer needs 4-byte-multiple sizes - rounded), textures +
   views (separate partitions), samplers (aniso requires all-linear - clamped), shader
   modules (SPIR-V via wgpu-native passthrough keyed on the SPIR-V magic; WGSL via the
   standard chained struct - the browser path, already tested). GOTCHA: wgpu returns an
   INVALID OBJECT + uncaptured error for bad descriptors, never null - the backend
   pre-validates (zero size/usage -> InvalidArgument).
   **STAGE 3 LANDED (same day) - bind groups + pipelines live**: bind-group layouts
   declare the DXC register-shift convention (ShiftedBinding: CBV 0 / SRV +1000 /
   UAV +2000 / sampler +3000, same table as vk::BindingShifts) so layouts match the
   desktop SPIR-V dev loop; the device requests the adapter's raised
   maxBindingsPerBindGroup at creation. PROVEN end-to-end: a WGSL pipeline declaring
   @binding(0)/@binding(1000)/@binding(3000) builds against a shifted layout.
   BIG FIND - PUSH CONSTANTS ARE REAL, NOT EMULATED: wgpu v29 exposes them as
   IMMEDIATES (WGPUNativeFeature_Immediates; immediateSize is in the STANDARD
   pipeline-layout descriptor and SetImmediates has standard procs, so browsers
   follow when the proposal ships). The planned dynamic-offset uniform-ring
   emulation is NOT needed on desktop; pipeline layouts declare immediateSize from
   the RHI's push-constant ranges. Also live: bind groups (positional entries ->
   shifted bindings), render pipelines (full state translation; Wireframe ->
   honest NotSupported, no polygon mode in WebGPU), compute pipelines, a benign
   empty PipelineCache (WebGPU caches internally).
   **STAGE 4 LANDED (same day) - encoders + swapchain; TRIANGLE RUNS (awaiting visual
   smoke)**: command pools (bookkeeping - WebGPU encoders are one-shot, wrappers
   re-open lazily which matches the RenderWindow Reset-and-reencode loop), command
   encoders (render/compute passes incl. pass-descriptor timestamps, all copies,
   debug labels, query resolve), render+compute pass encoders (SetPushConstants ->
   SetImmediates; multi-draw indirect unrolls; occlusion queries documented no-op -
   the RHI pass desc has no occlusionQuerySet field), render bundles (native
   concept, direct map), query sets (timestamp/occlusion; pipeline stats
   NotSupported), surface (X11/Wayland/Win32 chained sources), swapchain
   (configure + per-frame borrowed texture + present; no image index in the API -
   frame counter mod bufferCount). Barriers are no-ops (WebGPU tracks hazards).
   Blit = copy when extents/formats match; GenerateMipmaps/ResolveTexture deferred
   (blit-chain helper). PROOF: a real render pass clears an offscreen target, copies
   to a readback buffer, and the test verifies the pixels - full GPU round trip, 68
   assertions both compilers. Sample001_Triangle RUNS on --webgpu (SampleApp gained
   the flag) with zero validation errors on Wayland.
   TWO MORE v29 GOTCHAS: wgpuGetInstanceFeatures PANICS "not implemented" (like
   WaitAny) - the backend requests ShaderSourceSPIRV optimistically and falls back
   to a plain instance; SPIR-V ingestion is the STANDARD instance feature +
   WGPUShaderSourceSPIRV chained struct (the wgpu-native extension fn needs a
   device feature whose enum is commented out in the v29 header - avoided).
   **STAGE 5 LANDED (same day) - P1 BACKEND FUNCTIONALLY COMPLETE**: TransferBatch
   (records payload copies, replays as wgpuQueueWrite* at Submit - the runtime owns
   the staging ring; Submit blocks like the Vulkan batch, SubmitAsync signals the
   fence), real GetFormatSupport (WebGPU per-format capabilities are SPEC tables:
   depth family, BC feature-gated sampled-only, 32F non-filterable, integer
   non-blendable, snorm/packed sampled-only), and the HOST FRAME SEQUENCE PROVEN:
   the Smoketest's WebGPU section presents 3/3 real frames on the shell window
   (acquire -> clear pass -> fenced submit -> present) on the RTX 2060. 89 test
   assertions live on GPU, both compilers.
   **STAGE 6 (2026-07-30) - THE RHI SAMPLE BATTERY: 25/30 PASS, 5 GRACEFUL SKIPS,
   0 FAILURES** (user directive: the RHI samples are the backend's validation
   sweep; present-loop removed from the Smoketest). What the sweep forced:
   - BINDING SHIFTS NORMALIZED ENGINE-WIDE (user decision): ONE table
     CBV=0/SRV=+100/UAV=+200/Sampler=+300 (shaders::BindingShifts::Standard()),
     replacing Vulkan's 0/1000/2000/3000 - WebGPU validates binding indices
     against maxBindingsPerBindGroup (1000 in browsers, non-negotiable); Vulkan
     does NOT constrain binding indices (verified), so nothing is lost. Applied:
     vk::BindingShifts::standard(), ShaderSystem, UIRuntime, UISubsystem,
     UISandbox, samples ShaderHelpers, webgpu ShiftedBinding. Vulkan samples
     re-verified clean on the new table.
   - Fence waits ride wgpuQueueSubmitForIndex + DevicePoll(wait, &index): waits
     on EXACTLY that submission. Blanket DevicePoll(wait=1) can block FOREVER
     (empty queue, or presentation in flight on Wayland/FIFO); pumps otherwise
     use non-blocking polls + ProcessEvents. Buffer::Map logs loudly if a map
     request is left PENDING (a pending map keeps the buffer "mapped" and later
     submits fail).
   - ResolveQuerySet hops through an internal QUERY_RESOLVE|CopySrc scratch
     (WebGPU forbids resolving into mappable buffers).
   - Depth-texture sample type: a layout that binds a ComparisonSampler marks
     its sampled textures Depth (HEURISTIC; explicit BindGroupLayoutEntry field
     pending discussion). TimestampQuery + TimestampQueryInsideEncoders + BC +
     DepthClipControl requested whenever the adapter has them.
   - NEW RHI feature flag DeviceFeatures::occlusionQueries (Vulkan/DX12 true;
     WebGPU false until RenderPassDesc declares the query set at pass begin -
     the begin-anywhere shape cannot map). Samples 018/024 gate and skip
     gracefully, like mesh/RT.
   - Pre-existing SAMPLE bug fixed: [[rhi::vk::builtin/image_format]] rename
     artifacts in Samples 004/017/021/028 (broken on VULKAN too) -> [[vk::...]].
   **STAGE 7 (2026-07-30) - the three design decisions RESOLVED (user-approved)**:
   RenderPassDesc.occlusionQuerySet (WebGPU declares at pass begin; Vulkan/DX12
   get the set in the Begin call itself so need nothing; occlusion NOW WORKS on
   WebGPU - Sample024 reports correct counts; battery 26/30 pass, 4 graceful
   skips); BindGroupLayoutEntry.textureSampleType (Float/UnfilterableFloat/
   Depth/Uint/Sint - replaces the comparison-sampler heuristic);
   DRACONIC_APP_MAIN parses --vulkan/--dx12/--webgpu/--null-gpu (HelloWindow
   --webgpu runs the full GraphicsDevice/RenderWindow host path clean;
   project-settings selection layers on later via the settings/export track).
   REMAINING for the full renderer: GenerateMipmaps + scaling Blit (blit-chain
   helper pass). Renderer-level target: the Sandbox sample. Known emulations:
   - Push constants: 9 post shaders use the `SetPushConstants` RHI extension
     (`Extensions.cppm`); WebGPU has none -> backend emulates with a small dynamic-offset
     uniform ring, invisible to callers.
   - SPIR-V ingestion is a wgpu-native EXTENSION (Dawn/browsers refuse it) - it is the
     dev-loop bridge for existing DXC output only, never the shipping path.
   Milestone: the render thin slice (spinning cube) on wgpu-native, then the full renderer.
3. **P2 - cook-time WGSL** - owned by the SHADERS track (next conversation); web's hard
   requirement (HLSL -> WGSL at cook, runtime DXC survives as the desktop dev loop). This is
   the strongest argument carried into that conversation.
4. **P3 - web platform backends**: Emscripten runner callback path in ApplicationHost, SDL3
   Emscripten shell backend, VFS pak/fetch backend (cooked content preloaded or streamed),
   miniaudio WebAudio, JobSystem inline mode.
5. **P4 - export preset "Web"**: wasm + js + html shell + content pak through the existing
   (platform, config) export axis. Browser on-screen milestone: a cooked project playing in
   a browser tab.

## WebGPU feature notes (audited against the renderer)

- Bind groups: 4 max - renderer already complies (sets 0-3 only).
- Timestamp queries: optional feature - GPU GraphProfiler degrades gracefully when absent.
- BC texture compression: optional feature, present on desktop browsers; mobile browsers
  expose ASTC/ETC2 instead - cooked-texture format choice becomes a web-export knob later.
- Compute: available (fine for the future GPU particle sim).
- Readback: mapAsync only - screenshots/profiler readback must be async on web.
- Depth: Depth24Plus/Depth32Float naming differences handled in the backend format table.

## Deferred (none block a playing game)

- wasm pthreads + SharedArrayBuffer opt-in (hosting-header-gated).
- Networking on web: no UDP - a WebSocket (later WebRTC) transport behind the existing
  transport seam; desktop keeps reliable-UDP.
- Script debugger remote transport, native file dialogs, Memory64.
