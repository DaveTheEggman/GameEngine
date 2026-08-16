# UI core P2 - the canonical box model (border-box)

> STATUS: spec locked 2026-08-15 (Fable, from the audit in
> Documentation/Backlog/ui-core-audit.md Q1 - evidence anchors live there).
> Fable-executed, phased; battery green between phases. HiDPI on-screen
> verify (2x monitor) is QUEUED for UAT - the user is away from a 2x
> display; the DPI fix lands regardless (it is the spec, not a tweak).

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
- **SizeSpec is SELF-side and logical**: the base `Measure` applies the
  view's own Fixed/Match/Wrap spec, resolved with **dpi = 1** (layout runs
  in logical units; draw applies DpiScale once at the root). Parents stop
  interpreting child specs entirely - `MakeChildConstraints` + the
  FlexLayout/AbsoluteLayout clones are DELETED. This fixes both "Fixed
  works in 3 of 8 containers" and the Dp double-scale in one stroke.
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
- **P2d - wrap re-measure + rounding + measure dirty flags.** Label/text
  arrange-width re-wrap resolved (draw wraps at arranged width via the P1a
  draw cache; measured height must follow - re-measure on arrange-width
  mismatch); base Layout device-grid rounding lands here (after P2c so
  golden churn happens once); m_needsMeasure/m_needsArrange +
  InvalidateLayout bubbling + UpdateRootView early-out (the layout half of
  the P4 damage pipeline - the draw half stays P4).

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
