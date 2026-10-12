# Shaders, Materials & Hot Reload

> Status: CURRENT
> Verified: 2026-08-12 @ fd25f6de
> Track: [[renderer-direction]] / [[shaders-track]]

Edit an `.hlsl` shading body, a material asset, or a texture on disk and see it next frame - without
restarting, without renderer code knowing, and without crashing on a broken shader. One resource system
for everything; the only render-specific piece is the PSO cache. Shipped. (The shader source/cook/pack
layer is `Documentation/Systems/shaders.md`; this is the resource/material/PSO/reload architecture.)

## One resource system

`ShaderResource`, `MaterialResource`, `TextureResource` are products in the one `ResourceManager`
(`foundation.resource`), built by factories, handed out as `Proxy<T>`. Reload swaps a product behind a
`ResourceHandle`; a `Proxy<T>` holds the handle, so holders follow the swap. Consumers re-check a
version/identity and rebuild GPU objects lazily AT THE POINT OF USE - no listener bookkeeping, no
ordering hazards, naturally deferred. Everything is transactional: build the new thing, swap only on
success, keep the old + surface the error on failure. Dev-only: watch + compiler + version checks exist
in dev/editor; shipping loads cooked bytecode with none of it.

## Dependency tracking (shipped)

`foundation.resource`: `IResourceFactory::Create` takes a `ResourceManager&`; a factory that `Bind`s a
child mid-build AUTO-records the edge (a build-stack hook); `Reload(id)` is transitive (visited-guarded
for cycles, dependents snapshotted); forward edges clear on rebuild so stale deps drop; `Dependents(id)`
introspects. Resource->resource edges only (`MaterialResource -> ShaderResource` /`-> TextureResource`);
a directly-edited FILE maps to its resource by source path (a 1:1 lookup, not a graph), and everything
downstream is resource->resource propagation.

## Modules

- **`foundation.shaders.resource`** - `ShaderResource`: a shader's name in the `ShaderSystem` (its
  `(stage, flags) -> ShaderModule` variants compiled on demand via DXC, or served from the cooked
  pack), a version counter (bumped by every reload, `InvalidateShader`), and for a custom shader the
  depth and pick programs a material on it uses.
- **`foundation.materials`** - the data-driven material core: `PipelineConfig`, `MaterialBuilder`,
  `MaterialInstance`, the bind-group-inferring `MaterialSystem`.
- **`foundation.materials.pipelinecache`** - the PSO cache (the lone render-side standalone piece).
- **`foundation.materials.resource`** - `MaterialResource` (`MaterialSource` -> `MaterialFactory` ->
  `Material`), which Binds its `ShaderResource` by id and so records the material->shader edge.
- **`materials.pipeline`** - the material asset cook.

## The two reload paths (the crux)

A PSO bakes in shader-variant x material-render-state x vertex-layout x pass-RT-signature - render-state
and RT are not shader properties, so the PSO cannot live in `ShaderResource`. The renderer owns a
version-stamped cache keyed `(shaderId, featureFlags, renderState, vertexLayout, rtSignature)`; on fetch,
if `entry.version == shader.Version()` it returns, else it rebuilds from the current modules and
defer-destroys the old. Two INDEPENDENT paths meet at the per-frame draw:

| Path | Trigger | Rebuilds | Material's role |
|---|---|---|---|
| Asset reload | edit material / texture / mesh | resource products + bind groups | PARTICIPATES (its instances rebuild bind groups) |
| Shader/PSO reload | edit `.hlsl` shading math | PSOs only (version-driven) | does NOT participate (its set-2 layout is property-derived, stable across math edits) |

- Edit shader MATH -> `ShaderResource` reloads (its version bumps) -> only PSOs rebuild; material +
  bind groups untouched (the common case stays cheap).
- Edit a custom shader's INTERFACE (its properties block: a parameter added, removed or retyped) ->
  the shader re-cooks, and every material on it re-cooks through the recorded `reads` edge, laid
  out by the new table (values kept by name and type), so the layouts cannot diverge. The engine's
  own shaders have fixed layouts (`MaterialBuilder`'s templates). No binding-signature hash or
  reflection diff exists; the single declaration (the properties block) is what keeps them in step.

## File watching + GPU safety

Reload is driven by a portable POLLED change source: `NativeFileSystem::AsWatchable` exposes a change
queue via an O(files) stat-sweep (deterministic + portable, NO inotify/ReadDirectoryChangesW), drained
throttled once per ~second (fits the pull model). GPU safety: old modules / pipelines / bind groups may
be in flight, so they queue for deferred destruction `framesInFlight` frames later via the
`GraphicsDevice` ring; never tear down the old until the new succeeded AND the old is out of flight.
Compile errors surface (the console; for a custom shader, the shader page and `shader_validate`) and the
last good product keeps running: a failing cook keeps the old product, and a variant that fails at run time
is remembered (not recompiled every frame) until its source changes - never a crash on a bad edit.

## Deferred

- **`ShaderIncludeResource`** - editing a shared `.hlsli` of the engine's library reloads every shader
  the file provider serves (it does not know a header's includers) and retries every remembered
  failure, a registered custom shader's too; a custom shader's own product is not re-cooked by it. An
  include-as-explicit-resource (a normal resource->resource edge, no custom include handler) lands
  with project include files (backlog-0.2) or when the blanket reload becomes painful.

---

The design rationale (the version-poll-at-use vs dirty-flag decision, the Traktor precedent, the full
reload-flow walkthrough) is in `Documentation/Archive/shaders-materials-design-history.md`.
