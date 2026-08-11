# Shaders track - shaders out of C++, cooked per backend

Status: **decided (user, 2026-07-28).** The last of the three queued conversations (code
editor -> Emscripten/WebGPU -> shaders), held last on purpose: the other two supplied its
constraints (CodeEditView exists for the page; web hard-requires cook-time WGSL).

Decisions:
- **HLSL stays the authoring language.** The whole corpus (Sedulous-ported, assessed-correct)
  is HLSL; DXC is the working front end. WGSL is a cook-time TRANSLATION target, not a source
  language. Slang is the named fallback ONLY if translation proves lossy on our corpus.
- **All engine built-ins leave the C++ string banks** - the 14 Render banks (~2,800 lines) +
  VG + sample helpers become real `.hlsl` files under an engine content root (VFS-mounted),
  shared code via `.hlsli` (the ShaderSystem's DXC include-path seam already exists). Passes
  keep fetching by name; the C++ bank partitions are deleted as they migrate.
- **Dual-mode cooked form**: dev/editor keeps runtime-DXC compile-on-demand (instant hot
  reload, no variant pre-enumeration); EXPORT cooks declared variant sets to per-backend
  bytecode - SPIR-V (Vulkan) / DXIL (DX12) / WGSL (WebGPU) - along the existing
  (platform, config) export axis. **Shipped dists drop the DXC sidecar entirely** (retires
  the b11a902 fragility class).
- **SPIR-V -> WGSL via an external cook-time tool** (tint or naga-cli - pick by spiking both
  against the full corpus). Prebuilt binary the cook shells out to, needed only when cooking
  for web; no linked dependency (the wgpu-native/DXC sidecar spirit).

## What already exists (the architecture is settled; this track fills gaps)

- RHI takes BYTECODE only (`ShaderModuleDesc.code` = SPIR-V or DXIL) - source never crosses
  the RHI boundary.
- `draconic.shaders`: dlopen'd DXC `Compiler`; `ShaderSystem` = per-(name,stage) source
  registry + `ShaderFlags` (8 permutation bits -> #defines) + compile-on-demand variant
  cache + `.hlsli` include paths + version-bump invalidation that the PSO cache polls
  (hot reload propagation is DONE and proven).
- Project shaders are ALREADY file-based: `ShaderAsset` (vertex+fragment `.hlsl` files) ->
  `ShaderSource` cooked product -> `ShaderFactory` -> ShaderSystem registration; wired in
  Cook/Export/Editor Mains.
- The gaps: engine built-ins are C++ strings (rebuild per tweak, invisible to tooling);
  cooked product carries HLSL TEXT compiled by DXC on the END USER's machine (fragile
  sidecar, impossible on web); no WGSL; no Shader page.

## Resource model - two tiers (the renderer does NOT become resource-aware)

Question raised (user): do shaders become resources, and does the renderer have to learn
about the resource stack to reference them? Answer: two tiers, and the renderer stays
resource-agnostic - the layering already decides it. `draconic.render` links
Shaders/ShaderSystem but NOT `Draconic::Resource` (scene-agnostic AND resource-agnostic by
design; tests/samples/thin-slice drive it with no content DB). `draconic.render.subsystem`
is the layer that links Resource - the resource-aware bridge already exists.

- **Tier 1 - engine built-ins: files, not resources.** Name-addressed exactly as today
  (`GetVariant(name, stage, flags)` - pass call sites unchanged). The lift inverts source
  flow: passes stop PUSHING strings; the ShaderSystem PULLS through a provider seam:
  `IShaderSourceProvider` = dev: `engine://shaders/` via VFS -> HLSL text -> DXC on demand
  (+ file watch -> InvalidateShader); dist: cooked bytecode pack -> (name, stage, flags) ->
  blob (DXC absent; a miss is a loud cook-coverage error). Name convention =
  engine-relative path stem ("tonemap" <-> engine://shaders/tonemap.ps.hlsl). Provider
  eagerly scans the manifest (builtins are ENUMERABLE - the material editor's builtin
  dropdown needs the list) but lazily reads/compiles. Constructed where the Compiler
  already is (RenderSubsystem init). VG's bank joins the lift with its own provider hookup.
- **Tier 2 - project/user shaders: already resources, stay resources.** ShaderAsset ->
  cooked -> ShaderResource (Guid) -> ShaderFactory registers into the SAME ShaderSystem by
  name. Material->shader reload propagation is an existing resource edge.
- **The join point is the material resolve** (subsystem/materials layer), and the wire
  format ALREADY encodes both tiers (`MaterialSource.shaderId` Guid, nil -> `shaderName`
  builtin fallback). Either way the renderer receives what it receives today: a
  ShaderSystem name + flags.

Why built-ins are NOT Guid resources: render must stay drivable without a content DB;
builtins are referenced from CODE at fixed names (Guid indirection launders a compile-time
constant through a database); it would force an engine-side content DB (second DB,
versioning burden, mounted-before-renderer-boot) with no consumer; hot reload already has
its mechanism (ShaderSystem version poll). The only loss is uniform tooling, recovered
cheaply later: a dev-mode "open engine shader file" path in the Shader page (the page edits
text + compiles; it does not intrinsically need an Asset).

Cook split: project tier cooks per-asset via the existing ShaderAsset builder; engine tier
gets a dedicated engine-shader cook step in EXPORT (not the project asset pipeline) -
enumerate engine://shaders/, compile declared variant sets per target backend, emit the
bytecode pack, stage like the bundled fonts. Dev never runs it (dev reads files).

## Variant model - declared masks + canonicalization (decided, user, 2026-07-28)

The dist has no compiler, so the requirement is TOTALITY: every combination the runtime can
request must exist in the pack BY CONSTRUCTION, not by guess.

- **Authoring: in-source directive**, one comment line per stage file, uniform across both
  tiers: `// draconic:variants SKINNED INSTANCED`. Metadata travels with the source; the
  cook parses one line; the Shader page displays it; DXC never sees it. Absent directive =
  single-variant (mask None) - which is MOST shaders (measured 2026-07-28: only the
  mesh/forward family branches at all - SKINNED/INSTANCED/ALPHA_TEST/GBUFFER, 16 combos
  worst case; sky/sprite/decal/post consume zero #ifdefs; the other four ShaderFlags bits
  are currently declared-but-unconsumed).
- **Canonicalization**: `GetVariant` intersects EVERY request with the stage's declared
  mask, in dev AND dist identically. Cook builds the power set of the same mask ->
  any runtime request canonicalizes into the prebuilt lattice; a dist miss is impossible,
  not merely loud. Side payoffs: the runtime variant cache dedupes today (a PS that ignores
  SKINNED stops recompiling because a mesh was skinned), and dev == dist behavior.
- **Per-stage masks** keep the lattice honest (VS declares SKINNED/INSTANCED; PS declares
  ALPHA_TEST/GBUFFER -> 4+4 compiles, not 16+16).
- **Drift lint**: the cook scans each stage file for #ifdef tokens matching flag names and
  fails on USED-BUT-UNDECLARED (canonicalization would silently strip it - wrong visuals,
  the scheme's one failure mode). Declared-but-unused is a warning.
- **Enumeration: power set of the declared mask**, content-independent (scripts can spawn
  anything). Rejected: content-reachability pruning (reintroduces the dist-miss failure
  mode for runtime-composed content). Deferred until counts demand it: constraint syntax
  (exclusive groups, implies), reachability as an OPTIONAL optimization. The cook logs
  per-shader variant counts so growth is visible before it hurts.

## Phases

- **P1 - lift the built-ins.** STATUS 2026-07-30: SHIPPED for Render + Particles (all 15
  banks -> Data/Shaders/*.hlsl + 8 shared .hlsli; FileShaderSourceProvider over a
  NativeFileSystem mount with stat-sweep watch; every pass polls ShaderSystem::Version and
  rebuilds its pipelines on reload - proven live end-to-end). Deferred to later slices:
  VG + Imgui + samples (their explicit RegisterSource keeps working by precedence) and the
  PUSH_CONSTANT macro (files kept verbatim; do it with P3, which is what needs it).
  Original scope: Engine content root for shaders (VFS scheme, e.g.
  `engine://shaders/`), `.hlsli` extraction of the shared snippets the banks concatenate in
  C++ today, all 14 Render banks + VG + samples migrated, passes unchanged at the call site
  (fetch by name), file-watch -> ShaderSystem invalidate -> PSO rebuild (all existing seams).
  A `PUSH_CONSTANT(...)` portability macro replaces raw `[[vk::push_constant]]` declarations
  (expands to push-constant now; to a plain cbuffer at a reserved binding under the WGSL
  target later - pairs with the WebGPU backend's uniform-ring emulation).
  **Payoff on landing: edit a shader, see the result, zero C++ rebuild.**
- **P2 - cook-time bytecode.** Shaders declare their meaningful variant subset of the flag
  mask (no blind 2^8 enumeration); the ShaderAsset builder cooks per-backend bytecode blob
  sets; export stages only the target backend's blobs; ShaderSystem gains a bytecode-load
  path (GetVariant becomes a lookup on cooked platforms; failure to find a cooked variant is
  a cook-coverage bug, loudly logged). Dists lose the DXC sidecar.
- **P3 - WGSL target.** Tool spike (tint vs naga-cli) over the FULL corpus decides the
  translator; push-constant lowering validated; wired as a cook backend. Feeds the web
  track's P2 gate (docs/design/web-platform.md). **SPIKE DONE 2026-07-30 - see the section
  below; naga-cli is the translator, tint is the conformance oracle, 2 shaders need a
  uniformity fix before the browser milestone.**
- **P4 - Shader page** (editor page #9, the LAST one): CodeEditView (HLSL lexer + DXC
  diagnostics as Error/Warning markers - docs/design/code-editor.md P3 consumer), per-stage
  tabs, compile-on-edit, live preview viewport (material-on-mesh via the GPU-page recipe).

## P3 spike results - translator selection (done 2026-07-30)

Ran BOTH candidates over the full built-in corpus (55 stage files under Data/Shaders). Method:

1. Dumped every `.hlsl` stage to SPIR-V through the ENGINE'S OWN dev DXC path (a throwaway
   spike tool linking `draconic.shaders`, `Compiler::compile` with `BindingShifts::Standard()`,
   4 shift sets, `spirvTargetEnvironment = "vulkan1.1"`) - so the SPIR-V scored is exactly what
   `ShaderSystem::Compile` hands the WebGPU backend, binding shifts and all. 55/55 compiled.
2. Translated each `.spv` -> WGSL with both tools; scored coverage, binding-decoration fidelity,
   push-constant handling, round-trip validity, error quality.

### Version invariant (the decisive axis)

We ship **wgpu-native v29.0.1.1** (the newest wgpu-native release; there is NO v30 on the C
sidecar - the `wgpu` Rust crate v30 has not been wrapped by wgpu-native yet). It bundles
**wgpu-core / naga 29.0.x**, and that naga IS the WGSL validator our native WebGPU backend
links. So the translator with a real version invariant against our runtime is **naga-cli**,
pinned to the 29.0 line - officially installable (`cargo install naga-cli --version 29.0.4`,
reproducible), unlike tint, which has no Google prebuilt and tracks Chrome's Dawn, not our
runtime.

### Scores

| | naga-cli 29.0.4 | tint (Dawn, provisional binary) |
|---|---|---|
| Corpus coverage (SPIR-V -> WGSL) | **55/55** | 52/55 (3 rejected) |
| Round-trip (emit WGSL -> re-validate) | **55/55** clean, 0 warnings | n/a (tint is the validator) |
| Binding-shift fidelity | **exact** - CBV `@binding(0)`, SRV `100-109`, UAV `200-201`, Sampler `300-301`, all `@group(0)` | identical shift decisions on the 52 it emitted |
| Push constants (20/55 shaders) | `var<immediate>` (maps to wgpu-native Immediates - native-correct) | `var<immediate>` (agrees) |
| Version pinnable to our runtime | **yes** (naga 29.0.x) | no (tracks Dawn) |

### Decision: naga-cli is the translator; tint is the cook-time conformance oracle

Both tools agree on the hard parts (binding shifts, immediate lowering), and naga wins the
axis that matters for what we SHIP natively: it is the exact validator our wgpu-native links,
it is officially version-pinnable, and it cleared the whole corpus. So **naga-cli 29.0.x
translates SPIR-V -> WGSL in the web cook.**

The 3 tint failures are the spike's most valuable finding and give tint a permanent second
role. All three are **WGSL uniformity-analysis errors** - `textureSample` / `dpdy` called from
non-uniform control flow, which the WGSL spec makes a hard ERROR. **naga only WARNS on this;
tint (and therefore Chrome/Dawn in the browser) ERRORS.** Confirmed by feeding naga's own WGSL
output back through tint's frontend:

- `forward.ps` - REJECTED (conditional/material-dependent `.Sample()` in branchy flow)
- `taa.ps` - REJECTED (`CurrentColor.Sample()` inside the neighborhood `for` loop; the
  SPIR-V structurizer makes the loop data-dependent so uniformity can't be proven)
- `decal.ps` - tint's SPIR-V reader tripped on `dpdy(worldPos)` for the decal normal, but
  tint ACCEPTS naga's WGSL output for it, so it is fine via the naga path.

So naga alone would silently ship two browser-BROKEN shaders (fine on Vulkan/DX12 and on
Firefox/wgpu-native, dead on Chrome). **tint's role: run it over naga's WGSL at cook time and
fail the web cook on any uniformity/portability rejection** - the strict Chrome oracle naga
isn't. This cleanly divides the two tools instead of picking one.

### Follow-ups this spike created

1. **Fix the 2 uniformity casualties (browser gate).** Full-screen passes must not use
   implicit-derivative `.Sample()`; switch the offending sites to `SampleLevel(uv, 0)`
   (explicit LOD = no derivative = no uniformity requirement - taa.ps already does this for
   `HistoryColor`, just not `CurrentColor`/`DepthTexture`/`MotionVectors`). forward.ps needs
   its conditional material samples hoisted or `SampleLevel`-ed. Cheap, semantics-preserving.
2. **PUSH_CONSTANT macro (was P1 leftover, now P3's) - DONE 2026-07-30 (commit 6486dd9f).**
   `Data/Shaders/push_constant.hlsli` defines `PUSH_CONSTANT(Type, name, space)`; all 14
   declarations migrated (12 stage files + the shared ibl_common/bloom_common headers, fanning
   out to the 20 stage variants that emit `var<immediate>`). Default expansion = the old
   `[[vk::push_constant]]` form, byte-identical SPIR-V across the whole corpus (verified).
   Under `DRACONIC_PUSH_CONSTANT_AS_CBUFFER` (the web cook sets it, gated on the browser
   backend) it expands to a uniform buffer at the reserved binding: `space` -> `@group(space)
   @binding(0)` via the shift scheme (verified end-to-end - naga emits `var<uniform>`, tint
   accepts). Browser backend still owes the matching pipeline-layout side (bind group instead
   of a push-constant range; SetPushConstants -> uniform-buffer write) - the "later" half.
3. **Replace the provisional tint** (ThirdParty/Tint/PROVENANCE.md) with a pinned Dawn build
   for a reproducible oracle version. The uniformity rule is spec-stable, so the finding holds
   regardless; this is about reproducibility.
4. **Wire the cook backend**: naga-cli shell-out (SPIR-V -> WGSL) + tint validation pass, both
   cook-time only, never linked - the DXC/wgpu-native sidecar spirit. **Both are VENDORED
   PREBUILTS** (`ThirdParty/Naga`, `ThirdParty/Tint`) so nobody installs Rust or builds Dawn:
   naga-cli was produced once via `cargo install naga-cli --version 29.0.4` and the standalone
   binary (libc-only, no Rust runtime) checked in; players never touch either tool (dists ship
   cooked WGSL), and developers cook against the vendored binaries. Only the linux-x86_64 cook
   host is vendored so far - win-x64 / mac-arm64 naga binaries are a TODO (see PROVENANCE.md).

## Dependencies

- P4 needs CodeEditView P1+P2 (task #111). P3 gates the web browser milestone (task #112).
- P1/P2 are independent of both and can start immediately; P1 is the daily-workflow payoff.

## Non-goals

- A shader graph (authoring stays text; the material front-end/back-end split from
  renderer.md section 10 is a separate, later concern).
- Changing the variant model (flags -> defines stays) or the PSO-cache/hot-reload design
  (proven; this track feeds it files instead of strings).
