# Scene-pass MSAA

> Status: DRAFT (spec, ready to build)
> Track: backlog I11 / renderer
> Author: Fable, 2026-08-12. Opus executes; Fable reviews per phase.

**Motivation:** the Sponza-vs-Godot comparison (I11). Mips closed the
dominant gap; geometry-edge quality without TAA's temporal artifacts is the
remainder. Godot defaults to MSAA; our 3D pass is single-sampled with post
AA only (TAA/FXAA). Precedent in-tree: the UI canvas/world-panel path
already renders 4x MSAA with resolves, and PipelineStateCache already keys
`desc.multisample.count` from `config.sampleCount` - the PSO plumbing is
half-built.

## Decisions (settled here, not during the build)

1. **Sample counts: off / 2x / 4x, and 4x is the ceiling on web.** WebGPU
   guarantees 4x for the formats we use; 8x is a native-only capability
   probe (OPTIONAL follow-up, not in this track - do not build an 8x path
   the web target cannot validate). The count is capability-clamped at
   device init and the SETTING records intent, not the clamped result.

2. **MSAA is per-VIEW, not global.** The renderer is multi-view (editor
   viewport, game view, camera previews, web player). The sample count
   rides the same per-view surface as ViewPostOverride: the editor
   viewport gets a toggle in the existing post-flags menu; the game reads
   a project setting (`renderMsaaSamples`, default OFF for now - flipping
   the default is a one-line decision after perf numbers exist).

3. **The prepass problem - the load-bearing choice.** GTAO, SSR, and
   motion vectors consume the depth prepass at 1x today. A 4x main pass
   with a 1x prepass breaks early-Z (depth mismatch per sample) and
   starves post of matching depth. RULING: when a view is MSAA, the
   DEPTH PREPASS RUNS MSAA TOO (same sample state as the main pass -
   early-Z stays exact), and a RESOLVED depth (first-sample resolve -
   cheap, deterministic, and what the post effects effectively sampled
   before) is produced for every 1x consumer (GTAO/SSR/motion vectors/
   TAA history reprojection). Post effects stay 1x and UNCHANGED - they
   read resolved color + resolved depth. Accepted consequence: AO/SSR
   edges are computed from one sample per pixel (identical to today's
   quality); full per-sample post is out of scope permanently.

4. **Resolve points.** Scene color resolves ONCE, at the end of the
   forward pass (transparents included), via the render-pass resolve
   attachment (native on WebGPU; pResolveAttachments on Vulkan) - never a
   blit. Everything after (SSR composite onward through the post stack)
   runs on resolved 1x color. HDR RGBA16F resolve is supported on both
   backends; assert the capability at init rather than assuming.

   > **Implementation note (Opus 2026-08-12, for Fable review) - the 1x
   > consumers need resolved AUX buffers, not just depth.** Decision 3
   > says the prepass produces a resolved DEPTH for the 1x consumers. In
   > the code those consumers read more than depth: AO reads `normal`;
   > SSR reads `normal` + `material` + `velocity`; TAA reads `velocity`.
   > An MRT render pass forces every attachment to one sample count, so
   > the forward pass's aux G-buffers (`normal`/`velocity`/`material`)
   > become MSAA too and each needs a first-sample resolve alongside
   > depth - otherwise AO/SSR/TAA cannot sample them at 1x. This is the
   > forced consequence of "post stays 1x and reads those buffers," not a
   > scope change.
   >
   > **Mechanism chosen:** depth + the three aux resolve via a single
   > fullscreen SHADER pass that does `textureLoad(msaaTex, coord, 0)`
   > (sample 0) and writes the 1x outputs (a render pass, not a blit;
   > identical on Vulkan + WebGPU). Sample-0 (not an averaged resolve) is
   > what keeps AO/SSR/TAA "identical to today's quality" per Decision 3.
   > This is DISTINCT from the scene-COLOR resolve above, which correctly
   > uses the hardware resolve attachment (averaged - the real edge AA).
   > So under MSAA-on: 5 resolves total - color (hardware, averaged,
   > forward-end) + depth/normal/velocity/material (shader, sample-0,
   > after opaque forward).
   >
   > Rejected alternative for depth: a hardware depth/stencil resolve
   > attachment with `VK_RESOLVE_MODE_SAMPLE_ZERO_BIT`. Viable on Vulkan
   > but adds backend-specific depth-resolve plumbing and WebGPU expresses
   > it differently; the uniform shader pass avoids the split and also
   > covers the color-format aux (which have no sample-zero hardware
   > resolve mode at all). If Fable prefers the hardware depth path for
   > the depth specifically, that is a bounded swap of one resolve source.

   > **Fable RULING (2026-08-12): APPROVED - the aux-buffer consequence is
   > real and correctly derived, the shader-pass mechanism is right, and
   > the hardware depth-resolve rejection stands (backend split + covers
   > nothing the aux need). Three pins before building:**
   >
   > 1. **Depth resolves through SV_Depth into a REAL depth-format 1x
   >    target** - the resolve pass binds an MRT of the three aux colors
   >    PLUS a depth attachment written via SV_Depth (gl_FragDepth). Do
   >    NOT respell depth as an R32Float color target: every existing
   >    consumer (GTAO/SSR/TAA/motion reprojection) binds a depth-format
   >    texture today, and a format respelling would ripple through their
   >    bindings and shader declarations for zero benefit. With SV_Depth,
   >    every 1x consumer is untouched - which is the whole point of
   >    Decision 3.
   > 2. **The sample-0 nuance, documented where the shader lives:** sample
   >    0 of a standard MSAA pattern is NOT the pixel center, so resolved
   >    depth/normals sit ~0.4px off where a 1x prepass would sample.
   >    Consistent frame-to-frame and benign under TAA's own jitter -
   >    accepted. The rule it protects: depth is NEVER averaged (nonlinear
   >    - an averaged edge depth is a point in empty space).
   > 3. **Ordering belongs to the graph, not the prose:** "after opaque
   >    forward" over-specifies. The resolve pass is scheduled by its
   >    dependencies - after the LAST producer of depth+aux, before their
   >    FIRST 1x consumer - and the frame graph already orders that. If a
   >    consumer (GTAO) can run before transparents, the graph is free to
   >    schedule it so.
   >
   > Build on.

5. **MSAA and TAA are independent toggles.** They solve different
   aliasing (geometry edges vs shading/specular); both-on is legal and
   sometimes right (MSAA4 + TAA is the Godot-quality look). The editor
   post-flags menu shows both plainly. Document the cost note in the
   settings tooltip, not a hidden coupling.

6. **PSO matrix.** Every pipeline that renders INTO the scene pass
   (forward opaque/transparent, sky, decals, particles, sprites, debug
   draw, prepass variants) gains the sample count in its cache key -
   PipelineStateCache already carries `sampleCount`; the work is
   threading the VIEW's count into every PipelineConfig at that level
   (grep for configs constructed with the default). Cache re-key only -
   the bind-group versioning rules are untouched.

## What this deliberately does NOT touch

- Shadow maps, IBL/probe capture, canvas-RTT UI (has its own MSAA), the
  VG overlay (own MSAA), pixel-probe suites for VG.
- Alpha-to-coverage (foliage) - note as a cheap follow-up once counts
  plumb; not in this track.
- 8x, sample shading, per-sample post: out.

## Phases

**P1 - the multisampled view (the whole mechanism, editor-first).**
Frame-graph support for multisampled color+depth transient targets with a
resolve attachment; prepass + forward + in-pass consumers (sky/decals/
particles/sprites/debug draw) take the view's sample state via the PSO
key; first-sample depth resolve feeding the existing 1x consumers; the
editor viewport toggle (off/2x/4x) in the post-flags menu. Acceptance:
the pixel-probe harness grows a scene-pass case - a high-contrast edge
rendered at 1x vs 4x, asserting intermediate-coverage pixels appear on
the 4x edge (the VG MSAA test's precedent, applied to the 3D path);
GTAO/SSR/TAA/FXAA all still pass their existing probes with MSAA on;
both backends; wasm builds and renders (4x).

**P2 - the game setting + perf honesty.** `renderMsaaSamples` project
setting (player + play-in-editor honor it; capability-clamped), settings
UI, and MEASURED numbers on the render-stress scene (frame ms + memory
at 1x/2x/4x, desktop + web) recorded in the systems doc. Then - and only
then - the default-on/off decision goes to the user with numbers.

**P3 - polish.** Camera-preview inset inherits the main view's count;
export templates carry the setting; alpha-to-coverage spike if particles/
foliage want it (separate approval).

## Acceptance (track)

Both compilers + wasm green; the new scene-pass probe + full battery;
zero behavior change with MSAA off (the default) - single-sample paths
byte-identical in the probe; editor toggle user-verified on Sponza
against the Godot reference that motivated I11.
