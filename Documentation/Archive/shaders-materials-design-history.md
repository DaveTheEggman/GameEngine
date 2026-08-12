# Shaders, Materials & Hot Reload - design history (archived)

> Status: ARCHIVED
> Superseded by: Documentation/Systems/shaders-materials-hot-reload.md
> Track: [[renderer-direction]]

NON-AUTHORITATIVE. The design rationale behind the resource/material/PSO/hot-reload architecture.
Present-tense truth is `Systems/shaders-materials-hot-reload.md`; the full original doc is in git at the
P0 commit 3b92560d. Kept for the "why".

## Precedent

Traktor routes shaders through its resource system (`render::ShaderFactory : IResourceFactory`,
`IResourceManager::reload(guid)`, `Shader::getProgram(Permutation)`). We took the shape (one resource
system, factories, reload) and added a render-side PSO cache.

## Locked decisions

- **One resource system** - shaders + materials + textures are all resources with Guids, factories, and
  dependency edges. No parallel asset world.
- **Version-poll at point of use, NOT a dirty flag/event.** Compare the source's version (u64) to the
  consumer's stamped version WHEN you fetch the object to use it - never a per-frame scan. Per-frame cost
  equals a dirty flag (one read + branch where you touch the object), but it needs NO producer-side
  listener/back-reference bookkeeping and does nothing on the (rare) reload (consumers notice lazily).
  Strictly simpler at equal cost.
- **The PSO cache is the sole render-side standalone piece** - a PSO is shader x render-state x
  RT-signature, not an asset, so it lives in the renderer, version-stamped, lazy.
- **Dependency tracking is resource->resource only** (+ a 1:1 source-path->resource lookup for the
  directly-edited file). No file->resource graph.
- **Material does NOT reload on shader-math edits** - the set-2 bind-group layout is the material's
  (property-derived), validated against the shader by reflect-and-diff. On a shader INTERFACE change ->
  warn (do not silently auto-reload dependents; silent layout swaps hide authoring bugs).
- **Includes out of scope for v1**; a later `ShaderIncludeResource` (explicit reference, normal
  resource->resource reload) lands if shared-header iteration becomes painful.

## Reload flows (concrete)

1. Edit `forward.frag.hlsl` body -> `ShaderResource("forward")` reloads -> recompiles in-use variants ->
   signature unchanged, version++ -> PSO cache entries rebuild on next fetch -> new pixels. Material,
   textures, bind groups untouched.
2. Edit a shared `.hlsli` -> v1 no auto-reload (out of scope); later via `ShaderIncludeResource`.
3. Edit a texture -> `TextureResource` reloads -> material instances holding that texture proxy rebuild
   their bind group on next use.
4. Edit a material asset -> `MaterialResource` reloads -> instances rebuild bind groups (and fetch a
   different PSO key if render-state changed).
5. Edit shader INTERFACE -> signature hash changes -> reflect-and-diff vs the material layout -> warn.

## Note on what shipped differently

The original §6 proposed native OS file watchers (inotify / ReadDirectoryChangesW / kqueue). What
shipped is a portable POLLED stat-sweep (`NativeFileSystem::AsWatchable`, O(files), throttled ~1s) -
deterministic and portable, fitting the pull model - not native watchers.
