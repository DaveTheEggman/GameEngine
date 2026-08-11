# Shaders, Materials & Hot Reload — Design

Status: **mostly built (2026-06-26)**. Implemented + green on clang+gcc:
resource-manager dependency tracking (§4.2), the shader variant system
(`raptor.shaders.system`) + `ShaderResource`/`ShaderFactory`/`ShaderSource`
(`raptor.shaders.resource`) + shader editor cook (`raptor.shaders.editor`), the
data-driven material core (`raptor.materials`: PipelineConfig, Material/Builder/
Instance, the bind-group-inferring MaterialSystem), the PSO cache
(`raptor.materials.pso`, §4.4) with version-poll lazy rebuild + retire, the material
resource (`raptor.materials.resource`: `MaterialSource`→`MaterialFactory`→`Material`,
which Binds the `ShaderResource` by Guid and so records the material→shader dependency
edge, §4.5), and the material editor cook (`raptor.materials.editor`). The full
shader+material stack is complete — 41 tests green on clang+gcc.

Grounded in: `raptor.resource` (`ResourceManager` `Bind/Reload/Flush`,
`ResourceHandle` + `Proxy<T>`), `raptor.shaders` (stateless DXC `Compiler`),
`raptor.vfs` (`IWatchableFileSystem` seam), the `GraphicsDevice` frame-in-flight
ring, and the Sedulous-style data-driven material model. Precedent: Traktor routes
shaders through its resource system (`render::ShaderFactory : IResourceFactory`,
`IResourceManager::reload(guid)`, `Shader::getProgram(Permutation)`).

---

## 1. Goal

Edit an `.hlsl`/`.hlsli`, a material asset, or a texture on disk and see the
result next frame — without restarting, without renderer code knowing, and without
crashing on a broken shader. One resource system for everything; the only
render-specific piece is the PSO cache.

## 2. What we have / the gap

| Have | Where |
|---|---|
| Reload primitive: `Reload(id)` rebuilds a product + `Replace()`s it behind a `ResourceHandle`; `Proxy<T>` holds the **handle**, so holders follow the swap | `raptor.resource` |
| Reload-aware rebuild: the factory can be handed the current product to update in place | (add to `IResourceFactory`, à la Traktor's `create(..., current)`) |
| File-watch seam: `IWatchableFileSystem` (`AsWatchable()`), *"per-mount notifier for content changes, polled by consumers"* | `raptor.vfs` (not implemented) |
| Stateless DXC compiler: `compile(source, stage, entry, target, {defines, includePaths,…}) → bytecode` | `raptor.shaders` |
| Deferred GPU destruction substrate: per-frame fence ring | `GraphicsDevice` |
| **GAP**: no dependency graph (`Reload` is per-id; no "reload the dependents of X") | `raptor.resource` |

## 3. Principles

1. **One resource system.** Shaders, materials, textures, meshes, scenes are all
   resources with Guids, factories, and dependency edges. No parallel asset world.
2. **Proxies + version/identity + lazy rebuild *at the point of use*.** Reloads
   swap products behind handles; consumers re-check and rebuild GPU objects lazily
   when they next use them. No listener bookkeeping, no ordering hazards, naturally
   deferred.
3. **The PSO cache is the one render-side exception** — a PSO isn't an asset (it's
   shader × render-state × RT-signature), so it lives in the renderer, keyed and
   version-stamped, invalidated by shader-resource reload.
4. **Transactional + resilient.** Compile/build the new thing; swap only on
   success; on failure keep the old and surface the error. Iterating on broken
   shaders is the whole point.
5. **Dev-only.** Watch + compiler + version checks exist in dev/editor builds;
   shipping loads cooked bytecode with none of it.

## 4. Architecture

### 4.1 Everything is a resource

`ShaderResource`, `MaterialResource`, `TextureResource` are products in the one
`ResourceManager`, built by factories, handed out as `Proxy<T>`. Reload + dependency
propagation are resource-manager concerns, uniform across asset types.

### 4.2 ResourceManager dependency tracking — IMPLEMENTED (2026-06-26)

Built in `raptor.resource`: `IResourceFactory::Create` now takes a `ResourceManager&`;
a factory that `Bind`s a child mid-build **auto-records** the edge (build-stack hook);
`Reload(id)` is transitive (visited-guarded for cycles, dependents snapshotted);
forward edges are cleared on rebuild so stale deps drop; `Dependents(id)` introspects.
Resource→resource only (no file→resource — includes are out of scope / future
`ShaderIncludeResource`). Tests cover auto-edge + propagation, transitive chains
(each once), and stale-edge clearing; green on both compilers.

The original design follows.

Add a dependency graph so `Reload(id)` (or the watcher) can **reload dependents**.
**Resource → resource edges only** — declared by a factory when it builds a product
via the ids it resolves:
- `MaterialResource` → `ShaderResource`, `MaterialResource` → `TextureResource`.
- (later) `ShaderResource` → `ShaderIncludeResource` — see §4.3.

No `file → resource` edge: the watcher maps a directly-edited file → its resource by
**source path** (a resource knows its own source path — a 1:1 lookup, not a graph);
everything downstream is resource→resource propagation. This is small, reusable
(prefabs/materials/scenes want it too), and the Traktor-proven shape.

### 4.3 `ShaderResource` (the "different enough" machinery, encapsulated)

A shader is a resource, but its product owns the shader-specific complexity (so it
doesn't smear across the generic manager — your instinct, satisfied):

- **Variants**: a map `(stage, featureFlags) → rhi::ShaderModule`, compiled on
  demand via DXC (flags → `#define`s), cached. Same permutation model as Sedulous.
- **Includes**: out of scope for v1 — editing a shader *body* reloads; editing a
  shared `.hlsli` does not (shared headers change rarely; the body is the iteration
  loop). Later, add **`ShaderIncludeResource`** (an include is a resource a shader
  *explicitly* references) so a normal resource→resource edge reloads dependents —
  no custom include handler, no file→resource graph. Trade-off: the explicit
  reference can drift from the actual `#include`s.
- **Binding-signature hash**: a hash of the shader's *interface* (cbuffer/texture/
  sampler bindings, per space). Lets us tell a **math edit** (signature unchanged)
  from an **interface edit** (signature changed) — see §5.
- **Version counter**: bumped on every successful reload. The PSO cache reads it.
- **Transactional reload**: recompile the in-use variants; only on full success
  swap the modules + bump the version + defer-destroy the old modules; on failure
  keep old + report.

Consumers hold `Proxy<ShaderResource>` and ask it for `Module(stage, flags)`.

### 4.4 PSO cache (render-side, the lone standalone piece) — IMPLEMENTED (2026-06-26, `raptor.materials.pso`)

A PSO bakes in **shader variant × material render-state × vertex-layout × pass
RT-signature** — render-state/RT aren't shader properties, so the PSO can't live
inside `ShaderResource` (unlike Traktor's higher-level `getProgram`). So the
renderer owns a cache:

- **Key**: `(shaderId, featureFlags, renderState, vertexLayout, rtSignature)`.
- **Value**: `rhi::RenderPipeline` + the **shader version** it was built with.
- **Fetch**: if cached and `entry.version == shader.Version()` → return; else
  (re)build from the current modules, stamp the new version, **defer-destroy** the
  old pipeline. Lazy, version-driven — no listeners.

The renderer fetches by key every frame (never caches a raw PSO pointer across
frames) → transparently picks up rebuilt pipelines. **Zero renderer hot-reload
code.**

### 4.5 `MaterialResource` + `MaterialInstance` — IMPLEMENTED (2026-06-26, `raptor.materials.resource`)

Data-driven (the model we confirmed): a `MaterialResource` is authored content —
a shader reference, declared properties (which *define* the set-2 bind-group
layout), default values, texture references, and render state.

- **`MaterialResource`** (product): the shared definition. Depends on its
  `ShaderResource` (by id/name) and `TextureResource`s (by id) — resource edges.
- **`MaterialInstance`** (runtime): per-use param/texture overrides + the **cached
  set-2 bind group**. Created from a `Proxy<MaterialResource>`; holds
  `Proxy<TextureResource>` for each texture slot.
- **Bind group rebuild is lazy + version-driven** (same pattern as the PSO cache):
  on use, if any texture proxy swapped (texture reload) or the material def changed,
  rebuild the bind group, defer-destroy the old.

### 4.6 How the material fits — the junction of two reload paths

This is the crux. There are **two independent reload paths**, and the material
touches one but not the other:

| Path | Trigger | What rebuilds | Material's role |
|---|---|---|---|
| **Asset reload** | edit material / texture / mesh | resource products + bind groups (via dependency graph + texture proxies) | **participates** — material is a resource; its instances rebuild bind groups |
| **Shader/PSO reload** | edit `.hlsl` shading math | **PSOs only** (PSO cache, version-driven) | **does NOT participate** — its bind-group layout is *property-derived*, stable across shader-math edits |

The material couples to the shader's **interface** (the set-2 layout = the
material's declared properties — the convention), **not** its implementation. So:

- **Edit shader math** → `ShaderResource` reloads, signature hash **unchanged** →
  only PSOs rebuild. Material + bind groups untouched. (The 95% case stays cheap.)
- **Edit shader interface** (add/remove a texture or cbuffer field) → signature
  hash **changed** → the shader and the material's declared layout have diverged.
  This is a **validation error**, not an auto-reload: reflect the new shader, diff
  against the material's property layout, and surface a warning so the author
  updates the material declaration. (Optionally, reload dependent materials when the
  signature changed — but warn first; silent layout swaps hide authoring bugs.)

The two paths **meet at the renderer's per-frame draw**: it pulls a fresh PSO (shader
path) *and* binds the material instance's bind group (asset path) — both
transparently up to date, neither aware of the other.

## 5. Reload flows (concrete)

1. **Edit `forward.frag.hlsl` body** → watcher fires → `ShaderResource("forward")`
   reloads → recompiles in-use variants → signature unchanged, version++ →
   PSO cache entries for `forward` rebuild on next fetch → new pixels. *Material,
   textures, bind groups: untouched.*
2. **Edit shared `scene_uniforms.hlsli`** → v1: **no auto-reload** (out of scope;
   reload the dependent shaders manually or restart). Later, with
   `ShaderIncludeResource`, the resource→resource edge reloads dependents as in (1).
3. **Edit a texture** → `TextureResource` reloads (new GPU texture/view) → material
   instances holding that texture proxy rebuild their bind group on next use →
   new pixels. *Shaders/PSOs untouched.*
4. **Edit a material asset** (param/texture-ref/render-state) → `MaterialResource`
   reloads → instances rebuild bind groups (and, if render-state changed, fetch a
   different PSO key) → new pixels.
5. **Edit shader *interface*** → signature hash changes → reflect-and-diff vs the
   material layout → warn (and/or reload dependent materials). Not a silent path.

## 6. File watching

Implement `IWatchableFileSystem` for `NativeFileSystem` (dev): a per-mount,
**polled** change queue (inotify/ReadDirectoryChangesW/kqueue, drained once per
frame — fits our pull model). The runner drains it each frame, maps each changed
path → resource id(s) via the dependency graph (file→resource edges for shaders;
content-db path→id for assets), and calls `ResourceManager.Reload(id)` (+ reload
dependents). Debounce rapid successive writes (editors save in bursts).

## 7. GPU safety & resilience

- **Deferred destruction**: old `rhi::ShaderModule`/`RenderPipeline`/bind groups may
  be in flight → queue them for destruction `framesInFlight` frames later, via the
  `GraphicsDevice` ring. A small `DeferredDestroyQueue` keyed by frame index.
- **Transactional**: build-new-then-swap; never tear down the old until the new
  succeeded *and* the old is out of flight.
- **Error surfacing**: compile/link errors go to the console now, an editor panel
  later; the old product keeps running. Never crash on a bad edit.

## 8. Dev vs shipping

- **Dev/editor**: DXC compiler loaded, file watch active, version checks live,
  reflect-and-diff validation on.
- **Shipping**: cooked bytecode loaded by the shader factory (no DXC, no watch, no
  version checks). The `Proxy`/PSO-cache plumbing stays (it's cheap) but nothing
  ever reloads.

## 9. Decisions

**Locked (this design):**
- One resource system; shaders + materials + textures are all resources (Traktor
  precedent + your preference).
- The PSO cache is the sole render-side standalone piece; version-stamped, lazy.
- Dependency tracking is **resource→resource only** (+ a 1:1 source-path→resource
  lookup for the directly-edited file). No file→resource graph.
- **Includes out of scope for v1**; later add `ShaderIncludeResource` (explicit
  reference, normal resource→resource reload). No custom include handler.
- Material does **not** reload on shader-math edits; the set-2 layout is the
  material's, validated against the shader by reflect-and-diff. On a shader
  **interface** change → **warn** (do not auto-reload dependents).
- **Version-poll at point of use**, not a dirty flag/event. Rationale: "poll" =
  compare the source's version (u64) to the consumer's stamped version *when you
  fetch the object to use it* — never a per-frame scan. Per-frame cost equals a
  dirty flag (one read + branch where you touch the object); but it needs **no**
  producer-side listener/back-reference bookkeeping and does nothing on the (rare)
  reload (consumers notice lazily). Strictly simpler at equal cost.
- Uniform mechanism: proxies + version/identity + lazy rebuild-at-use + deferred
  destroy.

**Open:** none blocking. (`ShaderIncludeResource` lands if/when shared-header
iteration becomes painful.)
