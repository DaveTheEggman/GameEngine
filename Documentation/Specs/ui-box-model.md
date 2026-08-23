# UI core P2 - the canonical box model (border-box)

> STATUS: spec locked 2026-08-15 (Fable, from the audit in
> Documentation/Backlog/ui-core-audit.md Q1 - evidence anchors live there).
> Fable-executed, phased; battery green between phases. HiDPI on-screen
> verify (2x monitor) is QUEUED for UAT - the user is away from a 2x
> display; the DPI fix lands regardless (it is the spec, not a tweak).
>
> FOLLOW-UP (Opus, 2026-08-22): P2b fixed the unit SEMANTICS but the
> programmatic C++ layout call sites were never migrated - they used
> `Unit::Px` (post-P2b = fixed physical, does NOT scale with DPI) where they
> meant `Unit::Dp` (logical, scales). At 1.75 scaling that froze toolbars /
> panels / slider fields at physical size while DPI-scaled fonts+icons grew
> past them (PaperKid dogfood report). Migrated 65 control-sizing sites across
> 27 files in UI.Toolkit + Editor + UI/Overlay/Dialog `Px`->`Dp`; parsers
> (explicit author `px`), unit tests, and samples left as-is. clang+gcc +
> UI.Tests 734/734 green; the 2x on-screen verify is still the owed UAT.

## The contract (ONE box model, stated once)

- **MeasuredSize is the BORDER-BOX**: content + padding + border. Margin is
  NOT included (unchanged convention - margin belongs to the parent's
  aggregation), but margin handling moves INTO the base class so every
  container honors it identically.
- **Chrome resolves in one place**: `BoxMetrics { margin, padding, border }`
  via `View::ResolveBoxMetrics()` - margin from LayoutParams; padding =
  component-wise max of the ViewGroup `Padding` field, `StyleProperty::
  Padding`, and the background drawable's `DrawablePadding()` (Panel::
  EffectivePadding's max-merge, hoisted to the base and now including
  border); border = `StyleProperty::BorderWidth` (uniform thickness v1).
  The three padding channels stop having disjoint consumers.
- **Borders take LAYOUT space and paint INSIDE bounds**: border joins the
  chrome deflation, and bordered drawables stroke INSET (stroke center at
  borderWidth/2 inside the rect), so a bordered control measures larger by
  its border and never paints outside `Bounds` - clipping and siblings stop
  amputating/overlapping borders.
- **Fixed + margin are SELF-side and logical; Match stays parent-side**
  (AMENDED at P2b implementation): the base `Measure` deflates margin and
  applies the view's own **Fixed** spec, resolved in logical units (the
  only place Unit::Resolve is called for sizes - the Dp double-scale dies
  here). `Match` remains a parent-negotiated fill: base-side Match would
  tighten loose constraints and DEFEAT FlexLayout's deliberate cross-axis
  Match->Wrap demotion (a semantic pre-pass, not duplication) - and Match
  in a container with no fill concept (Flow/Grid) is meaningless anyway.
  `MakeChildConstraints` and the AbsoluteLayout clone are DELETED; the
  Match-supporting containers keep a tiny fill-vs-loose choice; FlexLayout
  keeps its demotion pre-pass minus Fixed/margin/dpi handling. Net: Fixed
  and margin work in EVERY container, DPI resolves once, and the three
  full spec-interpreter clones are gone.
- **Unit semantics (honest)**: `Dp(v)` resolves to `v` logical units.
  `Pt(v)` = `v * 96/72` logical. `Px(v)` = `v / dpiScale` logical - i.e.
  Px now truly means PHYSICAL pixels after the root draw scale (it was
  documented as raw pixels but was scaled like dp).
- **Layout rounds to the device grid**: base `Layout` snaps final bounds to
  `Round(v * dpi) / dpi`, and VG's `StrokeRoundedRect` joins the
  device-space snapping the axis-aligned primitives already have - the
  blurry-1px-border fix.

## The migration mechanism (how 30 controls move without one mega-commit)

`View::Measure` becomes a non-virtual template method with a SENTINEL seam:

    void Measure(BoxConstraints c);            // template method (base)
    protected:
    virtual void OnMeasure(BoxConstraints);    // LEGACY seam (deprecated)
    virtual Float2 OnMeasureContent(BoxConstraints contentConstraints)
    { return Float2{-1, -1}; }                 // sentinel = "not migrated"

Base `Measure`: deflate margin -> apply self SizeSpec (dpi=1) -> call
`OnMeasureContent(constraints deflated by padding+border)`; a non-negative
result is re-inflated by chrome and clamped. The sentinel falls back to
legacy `OnMeasure` with the post-margin post-spec constraints - which are
IDENTICAL to what FrameLayout children saw before, and newly correct for
children of the containers that ignored specs. Controls migrate to
`OnMeasureContent` batch by batch; when the last override of `OnMeasure`
dies, the legacy seam is deleted.

Containers keep margin AGGREGATION (row/column totals need child margins -
`MarginBoxSize()` helper = MeasuredSize + margin) but lose margin
PLACEMENT: base `Layout` offsets by margin once, uniformly.

## Phases

- **P2a - paint + metrics foundation (no measure-contract change).**
  BoxMetrics + ResolveBoxMetrics on View; Panel::EffectivePadding delegates
  to it (containers thereby gain STYLE padding - the first channel merge);
  RoundedRectDrawable strokes INSET + gains a DrawablePadding override
  reporting its border. Visible effect: borders stop bleeding outside
  bounds; content stops sitting on border lines in Panels. Battery-safe.
  (VG StrokeRoundedRect device-snap MOVED to P2d - it shifts probe pixels,
  and golden churn is batched there by design.)
- **P2b - the contract cut (one deliberate commit).** Base Measure template
  method + self-side SizeSpec (dpi=1) + margin moves to base (deflate in
  Measure, offset in Layout); DELETE MakeChildConstraints + the two clones
  + every container spec-switch and margin-placement block; ViewGroup's
  base OnMeasure/OnLayout asymmetry fixed (default = FrameLayout-shaped
  place-at-origin). Golden layout tests change WHERE margins/specs now
  count - that churn IS the spec change, reviewed value by value.
- **P2c - control chrome migration (batched).** Controls drop hand-rolled
  padding math and implement OnMeasureContent (Button family, EditText x8
  sites, Panel, ScrollView reserved-mode preserved); fill-style leaves
  (Separator/ProgressBar/Slider/ScrollBar/list views) switch MaxWidth-grabs
  to bounded defaults + Match specs - kills the kFloatMax explosions under
  Flow/Grid/ScrollView; one `IsBounded(f32)` helper replaces the three
  competing unbounded tests.
- **P2d - rounding (SHIPPED 2026-08-16).** Base Layout rounds the final
  border box to the device grid, EDGES independently so adjacent boxes stay
  gapless (composes globally - every ancestor rounds, sums stay on grid);
  VG StrokeRoundedRect snaps under axis-aligned transforms the way
  StrokeRect's bars do (centerline on the grid, device-rounded thickness,
  corners keep the analytic stroke; flips fall through). Zero golden churn
  - existing layout goldens were integral; VG probes green on GPU.
  RE-SCOPED OUT (recorded 2026-08-16, both moved to the P1b/P4
  damage-pipeline unit where they always belonged):
  (a) measure dirty flags + UpdateRootView early-out - implementing them
  exposed that ListView VIRTUALIZATION depends on per-frame relayout (rows
  realize in OnLayout; scrolling requires a layout pass), so the early-out
  needs the same producer sweep as the redraw gate - one unit, done
  together, with instrumentation + an escape hatch;
  (b) the wrap-text measured-height staleness - P1a's draw cache already
  fixed the v-align half (draw wraps at the ARRANGED width); the measure
  half needs a targeted re-layout loop, i.e. the damage pipeline. Until
  then it remains the pre-existing clip-at-cell-bottom behavior, no worse.
  **P2 - THE BOX-MODEL TRACK - IS OTHERWISE COMPLETE.**

## Acceptance

- LayoutTests goldens updated deliberately (each change justified in the
  commit message); new cases: bordered control measures larger by border,
  stylesheet padding works on a CONTAINER, nine-slice chrome reserves space
  under a Button, Fixed(100) child is 100 in EVERY container, margin
  honored in Flow/Grid, Separator in a GridLayout cell does not explode,
  Px/Dp resolve per the new unit table at dpi 1 and 2 (unit-level - the
  on-screen 2x check rides UAT when the user is at a 2x display).
- Full battery both compilers per phase; editor visual pass by the user at
  1x after P2b and P2c (chrome sizes will shift by design - themes carry
  1px borders everywhere, so every bordered control grows by 2px per axis
  unless the theme compensates; call this out in the UAT note).
