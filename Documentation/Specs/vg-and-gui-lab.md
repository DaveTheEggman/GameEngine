# VG first, then the experimental GUI as a scriptable lab

> STATUS: APPROVED 2026-10-14 (user direction: "fix VG first"; keep the experimental GUI alive as a
> smaller experimental UI framework, improve its shortfalls, build it on our RTTI so it is
> scriptable, and prove things there before they move into `foundation.ui`). Starts once
> Masquerade is finished, all its phases (user 2026-10-14). Read CONVENTIONS.md first.

## Why

A survey of our UI against LunarSong, RmlUi, AUI and eepp (2026-10-14; the gaps are in
`Plans/backlog-0.1.md`, "UI framework") found most of the polish gap in how our samples and themes
use the framework, but the deepest limits are in the drawing back end, VG, which both
`foundation.ui` and the experimental GUI draw through: it blends in linear light, clips to one
path at a time, has no offscreen layers, and draws a rounded box as three tessellated passes. So VG
comes first: whatever is fixed there lifts both frameworks.

The experimental GUI (`Code/Experimental/GUI`, eepp-derived, parked since 2026-09) has a better
style front end than `foundation.ui` (a real CSS cascade, `@media`, `@keyframes`, custom
properties, nesting, radial gradients, node transforms, a model/view layer) but applies only about
30 properties, animates only opacity (easing parsed and ignored), and is not scriptable (widgets
carry RTTI, no reflected properties). It becomes the lab: where the CSS-shaped model, effects,
animation, and a UI that is scriptable from the core are proven, then moved into `foundation.ui`
a piece at a time.

## Part 1: VG

Facts first (read 2026-10-14): VG's fills already include multi-stop linear, radial and conic
gradients with spread modes (`VG/Fills.cppm`); the renderer targets `BGRA8UnormSrgb` and decodes
vertex colours to linear (`VG.Renderer/Renderer.cppm`, `vg.vs.hlsl`); `PushClipPath` clips by
stencil one level deep (`VG/Context.cppm`); opacity is per vertex; rounded rects are tessellated
with a 0.75 px fringe (`VG/Tessellation.cppm`).

- **V1 Blend space.** A canvas chooses how it composites: Linear (today; right for in-world panels
  lit like the scene) or Encoded (sRGB-encoded values blended as browsers and design tools do; the
  default for UI). Half-black over white is then 50% grey, not 73%, and gradients and cross-fades
  interpolate as authored. Tests: pixel probes of a translucent fill, a gradient midpoint and a
  shadow edge in each space.
- **V2 Nested clips.** Clip paths nest (an intersection, not a replacement), so a rounded card can
  hold a scrolled list of rounded items. Tests: two nested rounded clips probed at their corners.
- **V3 Layers.** `PushLayer(opacity, blend)` draws a subtree into an offscreen target and
  composites it once, so a group fades as one (no children showing through each other, no
  double-blended cross-fades). Then filters on a layer (blur, desaturate, a colour matrix) and a
  backdrop filter (blur what is behind a panel). Tests: a group of overlapping shapes at 50%
  opacity probed for single blending; a blur's falloff.
- **V4 An analytic box.** A rounded box with border and shadow drawn as one signed-distance
  primitive (fill, border and shadow from one coverage function), so the three cannot disagree, a
  thin border has no halo, and small radii are exact; multiple shadows and per-side borders follow
  from it. The tessellated path stays for arbitrary paths. Tests: pixel probes against the
  tessellated result; a border with no outer halo.
- **V5 Device pixels under scaling.** A scaled canvas (a game UI at a reference resolution) lays out
  at its real scale, as DPI does, so edges land on device pixels; strokes of odd device width snap
  their centres. (This touches `foundation.ui`'s canvas scaler as well as VG.) Tests: a 2 px border
  at 0.48 scale lands on whole device pixels.

Each phase lands with its tests and is used by `foundation.ui` where it applies (its shadow radius
bug and its unused `opacity` are fixed alongside, from the backlog).

## Part 2: the GUI lab (`Code/Experimental/GUI`)

- **G1 Reflection and script.** Every widget and its style-facing properties reflected through our
  RTTI (`REFLECT_*`), so the engine's reflection-driven script binding (AngelScript and Luau)
  reaches the node tree with no separate UI-script layer: create and find nodes, set properties,
  add classes, listen to events, from either language. Markup, sheets, scripts and an inspector all
  read the same reflected table. Tests: both backends build a small tree, style it and receive a
  click.
- **G2 The applier catches up with the parser.** `box-shadow` (several), `transform` (translate,
  rotate, scale, skew, with an origin), gradients from sheets with angles and stops, per-side
  borders, `var()` everywhere. Tests per property.
- **G3 Motion.** Every animatable property transitions, with real easings (`cubic-bezier`,
  `steps`, the named ones); `@keyframes` animate any property, not only opacity and colour. Tests:
  sampled curves against the formula.
- **G4 Effects through VG.** Group opacity and filters through V3's layers, rounded clipping
  through V2, boxes through V4.
- **G5 Data binding.** Properties that update what depends on them (AUI's model: dependency
  tracking, no manual subscriptions), bound from markup and script. Tests: a bound label follows
  its source.
- **G6 A showcase.** The sandbox becomes a small scripted demo (a game menu with transforms,
  shadows, gradients, blur and animated states, written in AngelScript), the proof that it looks
  the part.

- **G7 Navigation and commands (must-haves, user 2026-10-14; every other phase designs for them).**
  `foundation.ui` has a spatial picker with code-only directional overrides (`View::NextFocusUp`
  and the rest, `FocusManager::MoveFocus`), an `ICommand` buttons bind to, and keyboard-only
  shortcuts (`ShortcutManager`); the lab has focus but no directional navigation.
  - **Spatial navigation, automatic by default**: a geometric picker takes the best candidate in the
    pressed direction (overlap on the cross axis preferred, distance weighed, hidden, disabled and
    clipped widgets skipped), so most screens need no authoring.
  - **Overrides where the geometry is wrong**: `nav-up`, `nav-down`, `nav-left`, `nav-right` (a
    widget's id, `none` to stop, `auto`) and `nav-next`/`nav-prev` for the tab order (document
    order otherwise), from markup, sheets and script.
  - **Focus scopes**: a container says what happens at its edge (pass to the neighbours, the default;
    wrap, for menus and carousels; contain, for dialogs); re-entering a scope returns to its last
    focused child; each screen or scope names its default focus.
  - **Gamepad feel**: a held direction repeats with acceleration; focus scrolls into view; the focus
    ring shows in gamepad and keyboard use only; a widget that uses the directions while active (a
    slider, a list) gives them back at its ends. A debug overlay draws the navigation graph.
  - **One command model**: a command has an id, a label, an icon, enabled and checked states and an
    execute, reflected so scripts define and run them; buttons, menu items, shortcuts and gamepad
    buttons bind to commands by id, so one disabled command greys every control bound to it.
  - **Bindings**: keys and gamepad buttons alike, scoped (global, a screen, the focused subtree) and
    routed by focus (the innermost scope wins), and fillable from the game's input map (Back is
    Escape and the East button). Once proven, `foundation.ui`'s `ICommand` and `ShortcutManager`,
    and perhaps the editor's action registry, converge on it.
  Tests: the picker over grid and irregular layouts, overrides and scopes (wrap, contain, focus
  memory), repeat timing, and a command's state reaching a button, a menu item and a binding.

## Moving into `foundation.ui`

Once a lab feature is proven (tests, the showcase, the user's look), it moves into `foundation.ui`
as its own layer, the editor and the samples keeping working at each step. The order is decided
then; likely the CSS front end and the motion first, the reflected object model last.

## Open questions

- V1: an Encoded canvas renders into a UNORM target and composites, or writes encoded values into a
  UNORM view of the sRGB target; to decide by measuring both on Vulkan and WebGPU.
- V3: the offscreen pool's budget on the web build.
- G1: whether the lab's widgets become `Object`s with reflected properties directly, or are
  described to the binding by a reflected facade; to decide in G1's first layer.
