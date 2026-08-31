# VG quality leftovers (post-#121)

> ARCHIVED 2026-09-01: still-open items live in Documentation/Plans/week-2026-09-05.md
> ("Backlog folder absorbed"). This archive keeps the full detail.


Size: M total; three independent items, land separately. Modules:
`Code/Draconic/Foundation/Draconic.VG` (Context.cppm),
`Code/Draconic/Foundation/Draconic.VG.Renderer` (Renderer.cppm),
`Code/Draconic/Engine/Draconic.Engine.Render` + `Draconic.Engine.UI` (overlay
tier), `Code/Draconic/Foundation/Draconic.VG.Backend.Tests` (probes).

#121 shipped: stencil-then-cover fills, host-provided stencil, window-host 4x
MSAA (UIRuntime), canvas-RTT + world-panel 4x MSAA (c1a161d5), gradient
spreads/blend modes/SVG gradients, clip-path via stencil (425c5098), the color
pipeline single-decode fix (16107056), and the backend pixel-probe suite.
These three were consciously deferred.

## A) Overlay-tier MSAA (scene.overlay + screen overlay passes)

The scene-tier and screen-tier game-UI overlays render single-sampled with a
stencil attachment (8ebcf4d6). Their VG edges are aliased where canvases are
now smooth.

- Scene tier (graph pass `scene.overlay` in
  Engine.Render/Pipeline{,Impl}: transient DS + SetDepthTarget): add a
  transient MSAA color the pass renders into, resolving into the view's
  target. The RenderGraph's pass/attachment model must support a resolve
  attachment on a graph pass - CHECK FIRST; if it does not, that plumbing (a
  `SetResolveTarget` on PassBuilder mapping to ColorAttachment::resolveTarget)
  is the real work and comes with its own graph test. Remember the f61d50e3
  lesson: any new attachment kind must RECORD A RESOURCE ACCESS or the pass
  silently drops - add a graph-level test that the pass executes.
- Screen tier (RenderSubsystem::RenderOverlays' cached window-sized DS): add a
  cached window-sized MSAA color next to the cached DS (resize -> retire queue
  via RenderSubsystem::RetireQueue(), same as the DS today), resolve into the
  backbuffer/target view.
- Both: the DS must move to sampleCount 4 to match, and
  UISubsystem::DrawRootInPass must be called with the pass's sampleCount (the
  parameter already exists; the renderer cache is already keyed
  format+stencil+sampleCount).
- OverlayView contract: SceneOverlayView/ScreenOverlayView should advertise
  sampleCount alongside depthStencilFormat so UISubsystem picks matching
  pipelines (mirror how stencil capability is advertised today).
- Web: verify on WebGPU - transient MSAA in the graph must not regress the
  destroyed-texture discipline (retire queue, no WaitIdle mid-frame).

Tests: extend the graph tests for the resolve access; on-device proof rides
the existing probe pattern only if an overlay-shaped harness is cheap -
otherwise assert pipeline sampleCount selection at the unit level.

## B) EvenOdd clip paths

`PushClipPath` v1 supports NonZero only (EvenOdd falls back to
scissor-of-bounds). The stencil bit-plane contract (kClipBit=0x80,
kWindingMask=0x7F, documented on VGFillPhase in Enums.cppm) already reserves
the winding bits; EvenOdd clip = the same ClipApply sequence but the winding
accumulation uses the invert-on-odd stencil op configuration the EvenOdd FILL
path already uses. Wire the fill-rule through PushClipPath into the
StencilWrite emission (Context.cppm EmitWindingFans caller) and pick the
matching clipped-write pipeline pair in Renderer.cppm (`ClippedWrite`).
Tests: VGContextTests clip-emission sequences for EvenOdd (mirror the three
existing clip tests) + a pixel probe: self-overlapping star clipped EvenOdd
shows the hole.

## C) Nested clips

v1 supports ONE clip level (PushClipPath asserts/ignores deeper). Design
options, in preference order:
1. Intersection-in-place: on nested push, re-run ClipApply against the
   EXISTING clip bit (readMask 0x80 test Equal + winding of the new path),
   producing the intersection in the same bit. Pop restores by re-emitting the
   outer clip's winding + ClipApply (requires the context to RETAIN outer clip
   path geometry on a stack - it already stores m_clipPathBounds; extend to a
   stack of tessellated fans or re-tessellate on pop).
2. Multi-bit planes (0x80, 0x40, ...) - rejected unless (1) proves
   impractical: it steals winding range and complicates every mask.
Scissor fallback: nested rect-ish clips still intersect via scissor when both
are axis-aligned (the cheap path today) - keep that fast path.
Tests: emission-sequence tests for push/push/pop/pop; pixel probe with two
overlapping clips proving the intersection region only.

## Acceptance

Each item lands independently with both compilers green, VG suites + backend
probes green, and (for A) user visual verify on desktop + web before closing.

---

## State (appended 2026-08-03; original content above is unchanged)

**DEFERRED (by choice).** #121 itself shipped (it is the Fable review baseline);
the three leftover items here stay consciously deferred until wanted.
