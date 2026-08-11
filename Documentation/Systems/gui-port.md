# GUI — an eepp-derived UI framework on Draconic

Status: **planning**. Decision locked: *fresh* `draconic.gui`, built bottom-up on
Draconic infra (VG + Fonts + Core + viewport-input), **progressively slicing eepp's
UI**, not lifting it wholesale. The in-flight Sedulous port (`draconic.ui`) stays
**parked** — GUI is a separate module; we may borrow its VG/Drawable *recipes* but
not its code.

Source of truth for the port: `/home/robert/Dev/CPP/eepp` (MIT license — clean, no
copyleft constraint). Namespace `EE::UI` / `EE::Scene` / `EE::Graphics`.

## Why progressive-slice, not wholesale-lift

A wholesale lift of `include/eepp/ui` + `src/eepp/ui` (186 hdrs / 177 srcs, ~118k LOC)
is a trap:

- **Dependency cone is most of the engine.** ui includes (by count): graphics ×202,
  system ×161, scene ×75, core ×62, window ×52, math ×34, network ×18, audio ×2.
- **No renderer seam exists.** ~19 files draw straight through `GlobalBatchRenderer` /
  `Graphics::Primitives` / `GLi->` immediate-mode GL. There is nothing to swap — the
  seam must be *invented*.
- **Constraint clash.** eepp is STL + exceptions + RTTI + raw-pointer ownership + its
  own scene graph. Draconic is named modules, `-fno-exceptions`/`-fno-rtti`, no-STL
  APIs, own containers, UTF-8, `Object` reflection. A wholesale lift means porting all
  of that at once with zero green feedback until the end.

Progressive slicing matches every prior Draconic port (RHI, VG, Fonts, Scene, the UI
work): bottom-up, subsystem-by-subsystem, tests green at each step, one vertical slice
before widening.

## The two seams (mapped against current eepp source)

### Tree foundation — `eepp/scene`
```
Math::Transformable            pure math (pos/rot/scale + cached 3x3 Transform). Clean.
  └─ Scene::Node               the tree base
       ├─ Scene::SceneNode     per-window root/coordinator
       │    └─ UI::UISceneNode
       └─ UI::UINode           tree content (visual chrome)
            └─ UI::UIWidget     CSS styling + layout
                 └─ widgets...
```
- **Node** = intrusive doubly-linked sibling list (`mChild/mChildLast/mNext/mPrev`),
  **raw-pointer parent-owns-children** (`childDeleteAll` → `eeDelete`), deferred delete
  via `close()` → SceneNode close-queue. Z-order = sibling order. Invalidation via
  `mNodeFlags` (VIEW_DIRTY/POSITION_DIRTY/POLYGON_DIRTY…) + `invalidateDraw()` walking to
  the owning draw-invalidator. Event registration (`addEventListener`), hit-test
  (`overFind`), coordinate conversion.
- **Coupling**: Transformable clean; `ActionManager`/`Action` clean (system only);
  **Node's `nodeDraw`/`matrixSet`/clip/`invalidateDraw` are renderer-bound** (GLi +
  GlobalBatchRenderer) — reimplement these against our DrawContext, keep the
  tree/event/action skeleton. **SceneNode + EventDispatcher are window-bound** (Window,
  Input, Cursor, FrameBuffer) — this is the platform integration seam.
- **Events**: two abstractions — low-level `NodeMessage` (`onMessage`, bubbles) and
  typed `Event`/`EventType` (MouseEvent/KeyEvent/FocusEvent/… + `EventCallback` map).
  Central `EventDispatcher` per SceneNode holds focus/over/down/dragging state.
- **Actions**: `Action` base + `ActionManager` (owned by SceneNode) + concrete
  Move/Resize/Fade/Rotate/Scale/Sequence/Spawn/Runnable/Delay… — directly portable.

### Render surface — everything funnels into 4 globals
The whole UI collapses into: `Graphics::Primitives` (stateful shape helper),
`GlobalBatchRenderer` (immediate-mode vertex batcher, one texture + one blend at a
time), `Text` (glyph-atlas quads, **HarfBuzz-shaped**), and `ClippingMask` + `GLi`
matrix stack. What the UI actually exercises:

| eepp op | freq | VG target |
|---|---|---|
| textured quad (image / region / nine-patch / **every glyph**) | very high | image paint + tint; text quads via font atlas |
| flat filled rect | very high | rect path + solid fill |
| rounded-rect fill (`Borders::createBackground`) | high | rounded-rect fill + AA |
| rounded/multi-color border (`Borders::createBorders`) | high | rounded-rect stroke, per-edge color/width |
| per-vertex-color quad/fan (gradients, soft-shadow) | med | linear/radial gradient paints |
| line rect / loop (focus/hover outlines) | med | stroked path + AA |
| arc/circle/polygon/soft-shadow | low-med | path fill/stroke; shadow → blur/radial |
| scissor clip stack | very high | clip-rect stack (intersection) |
| clip-plane clip (rotated/scaled subtree) | med | transformed clip / stencil |
| matrix push/pop | per rotated node | transform save/restore stack |
| blend / alpha / color-filter | per drawable | blend mode / opacity / paint tint |

**draconic.vg covers 100% of what eepp's UI uses.** Not used by the UI (pure upside
we get free): dashed strokes, conic gradients, explicit fill-rules, true path curves
(eepp pre-tessellates everything).

**Cleanest seam = the `Drawable::draw()` family.** The only files touching GPU
primitives are: `UIBackgroundDrawable`, `UIBorderDrawable`, `LinearGradientDrawable`,
`RadialGradientDrawable`, `UINodeDrawable`+`LayerDrawable` (CSS `background`
compositor), `Texture`/`TextureRegion`/`NinePatch`/`GlyphDrawable`, and `Text::draw`.
Reimplement those against a VG `DrawContext`; replace Primitives + GlobalBatchRenderer
+ ClippingMask + GLi with VG path/paint/clip/transform. Everything else in `src/eepp/ui`
is layout/state that calls only through these.

## Module shape

- `draconic.gui` (`Code/Draconic/GUI/`, namespace `draconic::gui`) — platform- and
  renderer-agnostic core. Links `draconic.core`, `draconic.vg`, `draconic.vg.svg`,
  `draconic.fonts`, `draconic.image`, `draconic.xml`. Exposes **abstract** input /
  clipboard / cursor / resource seams (pattern-B injected interfaces) — no window, no
  RHI, no GL.
- `draconic.gui.shell` — the bridge (reimplemented, not ported): feeds our
  `InputSurface`/`InputRouter` ([[viewport-input]]) into the core's abstract input seam;
  clipboard/cursor onto Shell. InputSurface consumption lives **here only**.

## Build order (bottom-up; each phase lands with tests)

0. **Skeleton + type adapters.** ✅ DONE. Module `draconic.gui` (`Code/Draconic/GUI/`,
   links Core+Policy) + CMake + Tests. Canonical vocab = core types (`Vector2f/Sizef→
   Float2`, `Color→core::Color`), plus two GUI-local primitives: `Core/Rect.cppm`
   (x/y/w/h storage — matches core::Rectangle + VG — with eepp Left/Top/Right/Bottom
   accessors) and `Core/Transform2D.cppm` (6-float column-vector affine; `ToMatrix()`
   feeds VG's `Float4x4`/`TransformPoint2D` convention losslessly). 22 test cases / 66
   assertions green clang+gcc.
1. **Transformable + Node (no draw).** 🟢 DONE (built after the render seam, per the
   reorder). `Transformable` (pos/rot/scale → cached `Transform2D`, radians) +
   `Node` (`Object`+`Transformable`; owning `Array<RefPtr<Node>>` children + non-owning
   parent; add/remove/reparent with keep-alive, z-order, size/bounds, world-transform
   accumulation + `ConvertTo{Node,World}Space`, `PointInside`/`OverFind` hit-test,
   visibility/enabled/alpha, bubbling `Invalidate`, `On*Change` hooks) + `Event`/
   `EventType`/`EventCallback` listener system. 64 cases/190 assertions clang+gcc.
   Actions/ActionManager + MutationQueue deferred to Phase 3 (need the scene coordinator).
2. **Render seam: `DrawContext` over VG + core drawables.** 🟢 DRAWABLES DONE.
   `DrawContext` (wraps vg::VGContext; clip/opacity/transform stacking, dpiScale) +
   `Drawable` base (Object, color/alpha, state-aware Draw) + full drawable set:
   `RectangleDrawable` (FillRect/FillRoundedRect), `BorderDrawable` (uniform
   StrokeRoundedRect; per-side deferred), `StateListDrawable` (+`ControlState`),
   `LinearGradientDrawable`/`RadialGradientDrawable` (multi-stop, VG*GradientFill over a
   rect path), `ImageDrawable`/`NineSliceDrawable` (VG DrawImage/DrawNineSlice), and
   `LayerDrawable` (the CSS-background compositor: stacked layers + insets, `Thickness`).
   Smoke tests assert real VG vertices (GPU-free); **★ vertical slice — panel (bg+inset
   border) composited through VG — is a passing test.** `Node::Draw(DrawContext&)` now
   walks the subtree (transform + opacity + optional child clip; background → OnDraw →
   children back-to-front → foreground), so **the whole tree renders through VG** — a
   nested panel tree is a passing test. 72 cases/199 assertions clang+gcc. **Phase 2
   COMPLETE.** Deferred to later phases: per-side border, full CSS background-position/
   size/repeat, rotated-node clip-planes (axis-aligned scissor only for now).
3. **SceneNode + coordinator + actions.** 🟡 COORDINATOR DONE. `SceneNode` (Node root:
   owns `ActionManager` + `MutationQueue`; `Update(Duration)` ticks actions → `OnUpdate`
   hook → drains deferred edits; `GetActionManager`/`GetMutationQueue` overrides that
   Nodes reach by walking up). `MutationQueue` (deferred ops). `Action`/`ActionInterpolation`
   + `ActionManager`; actions `Move`/`Fade`/`Scale`/`Delay`/`Runnable`/`Sequence`. `Node`
   gained `GetRootNode`, `RunAction` (routes to the coordinator), `Close` (deferred
   removal via the queue). 83 cases/226 assertions clang+gcc.
   `EventDispatcher` 🟢 DONE: platform-agnostic Inject* API (fed by the bridge, not a
   window) → `OverFind` hit-test → hover enter/leave, press/release→click, click-to-focus,
   key/text routed to the focus node; `SetFocusNode`/`NotifyNodeRemoved`. `Node` gained
   input `On*` hooks + `Handle*` dispatch + `RequestFocus`/`ReleaseFocus` (the last via a
   `.cpp` impl unit, `GUIClusterImpl.cpp`, breaking the Node↔dispatcher partition cycle);
   typed `MouseEvent`/`WheelEvent`/`KeyEvent`/`TextInputEvent`. `SceneNode` owns the
   dispatcher. **★ hover/click/focus on a panel is a passing test.** 89 cases/251 assertions.
   `gui.shell` bridge 🟢 DONE: separate module `draconic.gui.shell` (`GuiInputBridge`) -
   the ONLY GUI module that imports the platform input layer (`draconic.shell`). Translates
   `shell::InputEvent` → dispatcher `Inject*`, mapping platform enums (MouseButton order
   differs!) → GUI enums and window→content positions via `ContentFit::ToContent`. 5 cases
   /11 assertions. **Phase 3 COMPLETE — platform input → bridge → dispatcher → tree works
   end-to-end.** Deferred: drag/capture; node-removal → `NotifyNodeRemoved` wiring;
   InputSurface gating/pump convenience. (`UISceneNode` = UI phase; `SceneNode`→`RootView`
   rename at end-of-port.)
4. **Text/Font.** 🟢 DONE. `Text` (Text/Text.cppm) over `draconic.fonts`: holds a
   non-owning `CachedFont*` + string + color + H/V alignment; measures via
   `IFont::MeasureString`/`Metrics().lineHeight`; `AlignedPosition(bounds)`; draws via
   `ctx.VG().DrawText(text, CachedFont*, pos, color)`. GUI links `Draconic::Fonts`.
   Tested against a mock `IFont` (measurement + alignment + draw guards); the full
   glyph-render path (atlas+texture via the font service) is an integration concern.
   94 cases/270 assertions clang+gcc. Deferred: per-range colors/outline/shadow, wrap,
   HarfBuzz/bidi, a `TextDrawable` wrapper.
5. **UINode / UIWidget chrome.** 🟢 DONE. `UINode` (Widgets/UINode.cppm): padding +
   `GetContentBounds()`; `SetSkin` (state-aware background); **input-driven
   `GetControlState()` override** (disabled>pressed>hover>focused>normal) fed by
   `OnMouse{Enter,Leave,Down,Up}` - so a StateListDrawable skin reacts to input with zero
   per-widget wiring. `UIWidget` (Widgets/UIWidget.cppm): CSS identity (tag/id/style
   classes add/remove/toggle/has - the Phase-6 selector surface) + layout margin.
   (Background/foreground/clip already on Node from the render-seam phase.) 101 cases/302
   assertions clang+gcc. Deferred: size policies, tooltips, attribute/markup load.
6. **CSS engine** (`ui/css`, ~10.5k LOC — the crown jewel). 🟡 IN PROGRESS (sub-increments).
   6a `StyleSelector` DONE (Styling/StyleSelector.cppm): compound `StyleSelectorRule`s
   (tag/#id/.class/:pseudo) joined by descendant/child combinators; packed specificity
   (id 1048576 / class 1024 / tag 1, eepp-matching); `Select(UIWidget&)` matches the
   rightmost rule then walks ancestors. **Pseudo-classes (:hover/:active/:focus/:disabled)
   map onto the Phase-5 control-state flags** — CSS reacts to input for free. Common
   subset; deferred: sibling combinators, structural pseudo, attribute selectors, :not.
   105 cases/329 assertions clang+gcc.
   6b Cascade DONE (Styling/StyleRule.cppm, StyleSheet.cppm): `StyleProperty` (name/value
   strings), `StyleRule` (selector + declarations), `ResolvedStyle` (flat cascaded set),
   `StyleSheet.Resolve(UIWidget&)` — gathers matching rules, stable-sorts by specificity
   (ties keep source order → later wins), applies low→high. 111 cases/343 assertions.
   6c `.css` parser DONE (Styling/CSSParser.cppm): `CSSParser::Parse(text)→StyleSheet` -
   strips `/* comments */`, splits comma selector lists (one rule each), parses
   `name: value;` blocks; best-effort/tolerant. Authorable CSS now resolves end-to-end
   (parse → cascade → ResolvedStyle on a widget). 117 cases/356 assertions.
   6d Typed application DONE (Styling/CSSValues.cppm, StyleApplier.cppm): value parsers
   (`ParseColor` named/#hex/rgb(), `ParseLength`, `ParseBool`, `ParseThickness` CSS
   shorthand) + `ApplyStyle(UINode&, ResolvedStyle)` writing background-color/padding/
   margin/opacity/width/height/enabled/visibility onto widget setters. CSS now *styles*
   widgets end-to-end (parse → resolve → apply). Generic string helpers (IsWhiteSpace/
   IsDigit/IsHexDigit/HexValue/Trim) moved to **core (`:string_util`)** per user; GUI
   `:parse_util` keeps only the CSS identifier scanner. GUI 124 cases/403 assertions;
   core +StringUtil tests.
   6e `!important` + custom properties DONE: StyleProperty.Important; parser strips trailing
   `!important`; `ResolvedStyle.Set` resists non-important overrides (importance wins over
   specificity); `ResolveVariables` substitutes whole-value `var(--x[, fallback])` against
   the cascaded `--custom` props. 131 cases/412 assertions.
   6f `@media` queries DONE (Styling/MediaQuery.cppm): `MediaQuery` (min/max-width/height
   feature tests combined with `and`) + `MediaContext` (viewport w/h/dpi); parser handles
   `@media (cond) { rules }` via brace-matched recursion attaching the query to each rule;
   `StyleSheet.Resolve(el, MediaContext, applyPseudo)` skips rules whose query doesn't match.
   137 cases/430 assertions.
   6g CSS transitions DONE (Styling/Transition.cppm): `ParseTransitions` (the `transition`
   shorthand) + `ApplyStyleAnimated(node, old, new)` — a transitioned `opacity` change
   spawns a `FadeAction` through the ActionManager (**the CSS ↔ Phase-3 Action-system
   integration**); non-transitioned props snap. v1 animates opacity; size/pos/color follow
   the same shape. 141 cases/446 assertions.
   6h `StyleManager` DONE (Styling/StyleManager.cppm) — **CSS is now live on the tree.**
   Holds a StyleSheet + MediaContext + a per-widget last-applied `ResolvedStyle` cache;
   `ApplyTree(root)` resolves each UIWidget and applies (animated diff vs cache);
   `SetStyleSheet` = hot-reload (drops cache); `SetMediaContext`; `Forget`/`Clear`. The
   showcase test: **hovering a widget re-resolves `:hover` and drives its transition through
   the ActionManager, live.** 145 cases/458 assertions. **CSS engine essentially complete**
   (selector/cascade/parser/typed-apply/!important/vars/@media/transitions/live-manager+
   hot-reload). Remaining niceties: background-image→Drawable*/font-family→Font* (resource
   provider); `@keyframes` runtime; @import; dirty-tracked re-resolve; partial/nested var().
7. **Widgets, thinnest-first.** 🟡 IN PROGRESS.
   7a `Label` + `Button` DONE (Widgets/Label.cppm, Button.cppm): `Label:UIWidget` draws the
   Text primitive in its content bounds (text props + measure); `Button:Label` centers text,
   defaults the `button` tag, fires an OnClick callback on MouseClick. Everything composes -
   a test drives a Button through the dispatcher (click, control-state) AND styles it via CSS
   `button:hover` through the StyleManager. 151 cases/473 assertions.
   7b `LinearLayout` DONE (Widgets/LinearLayout.cppm) + `Node::OnChildrenChanged` hook:
   stacks children in a row/column with spacing from the content bounds; re-runs on add/
   remove/size/orientation/spacing change; skips hidden children. Children keep their own
   sizes (measure/weights/gravity deferred). 156 cases/486 assertions.
   Widgets are **thin Draconic-native** (capture eepp roles, not line-for-line ports — user-
   confirmed).
   7c `GUISandbox` DONE (Code/Samples/GUI/) — **on screen**: styled panel + labels + buttons,
   live hover/click/CSS via VG→VGRenderer→RHI; input polled from a fullscreen `InputSurface`
   (added `GuiInputBridge::PumpFromSurface`). Fixed a Text vertical-centering bug (VG anchors
   at baseline; delegate bounds-draw to VG's alignment overload).
   7d `CheckBox` DONE (Widgets/CheckBox.cppm): toggle on click + `OnCheckedChanged`, draws box
   outline + inner check; in the sandbox toggling it highlights the counter. 160 cases/500 assertions.
   TODO: Radio → Slider/ScrollBar → GridLayout → ListBox/ComboBox → Window/TabWidget → models.
   Heavy widgets (CodeEditor, HTML/Markdown, Terminal) opt-in later; keyboard/text input in the
   sandbox still to wire.
8. **Markup + tools.** XML/CSS layout loading on `draconic.xml`; models; tools dir.

## Decisions (locked)

- **Ownership** — **RefPtr-owned children + MutationQueue** for deferred delete
  (mirrors eepp `close()`), *not* raw-pointer parent-owns. Safer under
  `-fno-exceptions`, matches Object, and the parked `draconic.ui` proved the pattern.
- **Naming** — **adapt eepp camelCase → Draconic PascalCase** methods
  ([[naming-convention-pascalcase]]); private members `m_`. Localized cost: no
  line-by-line diffing against eepp source.
- **RTTI** — `Node`/`Drawable`/`Action` derive `Object` + `DRACONIC_OBJECT`; eepp
  `dynamic_cast`/type-bits → `Cast<T>`/`IsA<T>`. Enables scriptability + markup
  instantiation.
- **Text shaping** — use `draconic.fonts` `TrueTypeTextShaper` now (ASCII/Latin);
  defer eepp's HarfBuzz+bidi (its unique strength) until rich-text widgets need it.
- **Tests** — every phase lands with doctest tests in `GUI/Tests/`
  ([[tests-required-for-additions]]). Where eepp has no tests, we author them for
  value types / tree / styling.
