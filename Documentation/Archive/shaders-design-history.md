# Shaders track - design history (archived)

> Status: ARCHIVED
> Superseded by: Documentation/Systems/shaders.md
> Track: [[shaders-track]]

NON-AUTHORITATIVE. The decisions + phase history + the P3 translator spike behind the shaders-out-of-C++
track. Present-tense truth is `Systems/shaders.md`; the full original doc is in git at the P0 commit
3b92560d. Kept for the "why".

## Sequencing

The last of three queued conversations (code editor -> Emscripten/WebGPU -> shaders), held last on
purpose: the other two supplied its constraints (CodeEditView exists for the page; web hard-requires
cook-time WGSL).

## Phases (as shipped)

- **P1 - lift the built-ins** (the ~15 Render + Particles C++ string banks + VG become `.hlsl` files
  under an engine content root): SHIPPED. The C++ bank partitions were deleted as they migrated.
- **P2 - cook-time bytecode** (declared variant subset -> per-backend `ShaderPack`, SPIR-V/DXIL):
  SHIPPED.
- **P3 - WGSL target** (SPIR-V -> WGSL via an external cook-time tool): SHIPPED with naga.
- **P4 - the Shader editor page** (editor page #9, the LAST): NOT built (still deferred).

## P3 spike - translator selection (naga over tint)

The spike ran tint vs naga-cli over the full corpus. naga was chosen: it round-tripped the corpus with
`--keep-coordinate-space` (the load-bearing flag that unifies WebGPU clip space with Vulkan - see
[[web-render-fixes-and-flip]]), and the PUSH_CONSTANT macro rework (commit 6486dd9f) was the one source
change needed. The translator is a prebuilt binary the cook shells out to (the wgpu-native/DXC sidecar
spirit), needed only when cooking for web; no linked dependency.

## Key rulings

- HLSL stays the authoring language; DXC is the front end; WGSL is a cook-time translation target, not a
  source language; Slang was the named fallback only if translation proved lossy (it did not).
- RHI takes bytecode only; source never crosses the RHI boundary.
- Dual-mode: dev keeps runtime-DXC compile-on-demand (hot reload); export cooks to a per-backend pack;
  a shipped dist drops the DXC sidecar (retiring the b11a902 sidecar-fragility class).
