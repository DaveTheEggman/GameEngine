# Shaders - out of C++, cooked per backend

> Status: CURRENT
> Verified: 2026-08-12 @ fd25f6de
> Track: [[shaders-track]]

Engine shaders are HLSL source `.hlsl` files (not C++ string banks), compiled on demand by runtime DXC
in dev and cooked to per-backend bytecode for export. Shipped: the source lift (P1), cook-time bytecode
(P2), and the WGSL translation target (P3). The dedicated Shader editor page (P4) is the one deferred
piece.

## Decisions

- **HLSL is the authoring language** (the whole ported corpus is HLSL; DXC is the front end). WGSL is a
  cook-time TRANSLATION target, not a source language.
- **All engine built-ins live as `.hlsl` files** in the data root's `Shaders/` folder, read through the
  application's data mount (`Documentation/Systems/data-root.md`), sharing code via `.hlsli` - served
  to DXC by the file provider as the compiler's include resolver (no native include paths). Passes
  fetch by name. The old C++ string-bank partitions are deleted.
- **Dual-mode cooked form**: dev/editor keeps runtime-DXC compile-on-demand (instant hot reload, no
  variant pre-enumeration); EXPORT cooks declared variant sets to per-backend bytecode - SPIR-V (Vulkan)
  / DXIL (DX12) / WGSL (WebGPU). A shipped dist drops the DXC sidecar entirely.

## Modules

- **`foundation.shaders`** (`Code/Foundation/Shaders`, `+ /Cook`) - the dlopen'd DXC `Compiler`, the
  `ShaderPack` cooked format (`ShaderPackCooker`), and the `WgslTranslator`.
- **`foundation.shaders.resource`** (`Shaders.Resource`) - `ShaderResource` / `ShaderFactory` /
  `ShaderSource` / the `ShaderSystem` variant registry.
- **`foundation.shaders.system`** (`Shaders.System`) - the host that picks dev-compile vs pack mode
  over the data mount (`ShaderSystemHost`: `Shaders/` sources, or the cooked `Shaders/shaders.dpak` -
  `kShaderPackPath`) + `FileShaderSourceProvider` (the hot-reload source AND the include resolver).
- **`shaders.pipeline`** (`Pipeline/Shaders.Pipeline`) - the cook integration.
- **`Tools.ShaderPack`** - the CLI shader-pack cooker.

## Variant model + cooked pack

RHI takes BYTECODE only (`ShaderModuleDesc.code` = SPIR-V / DXIL; source never crosses the RHI
boundary). `ShaderSystem` is a per-(name, stage) source registry + `ShaderFlags` (permutation bits ->
`#define`s) + compile-on-demand caching. Cook: shaders declare their meaningful variant subset of the
flag space; the cooker compiles each `(name, stage, variant, backend)` to a `ShaderPack` blob (SPIR-V /
DXIL bytecode, or WGSL text), binary + versioned + self-describing (carries the name table so the
runtime provider resolves by name). A shipped dist reads the pack and carries no DXC or naga.

## WGSL target (naga)

`WgslTranslator` cooks HLSL -> (DXC, vulkan1.1 target env + engine Standard binding shifts) -> SPIR-V ->
(naga) -> WGSL, at cook time on the dev/CI host only. naga was chosen over tint by a spike over the full
corpus. `--keep-coordinate-space` is load-bearing (the WebGPU clip-space unification - see
[[web-render-fixes-and-flip]]). Export stages the WGSL pack for the Web platform; desktop stages the
SPIR-V/DXIL pack (see `Documentation/Systems/export.md`).

## Hot reload

Dev hot reload is a portable POLLED stat-sweep, NOT native OS watchers: `NativeFileSystem::AsWatchable`
exposes a change source (no inotify/RDCW plumbing - deterministic + portable), and
`FileShaderSourceProvider::PollChanges` stat-sweeps every ~60th call (~1s at 60fps, throttled because
the sweep is O(files)). The render subsystem calls `m_shaders->PumpReloads()` each frame and idles the
GPU on a reload. Details of the resource-swap + PSO-invalidation plumbing are in
`Documentation/Systems/shaders-materials-hot-reload.md`.

## Deferred

- **P4 - the Shader editor page** (editor page #9): a `CodeEditView` HLSL page with DXC diagnostics as
  Error/Warning markers, per-stage editing, and a preview. NOT built (no `Editor.Shaders` / `ShaderPage`
  exist yet); this is where the code editor's deferred HLSL lexer lands.

---

The P3 translator spike (tint vs naga comparison over the corpus) and the phase-by-phase history are in
`Documentation/Archive/shaders-design-history.md`.
