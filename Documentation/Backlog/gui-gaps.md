# draconic.gui vs eepp UI — gap analysis (2026-07-09, HEAD 665fce5)

Where the port stands and what's missing, to plan the next sessions. eepp UI ≈ 115k LOC,
~95 widgets, real CSS, HarfBuzz+bidi text, models/MVC, XML markup, code editor/HTML/terminal.
draconic.gui = the **engine + CSS + 5 widgets**, thin-native (roles not source).

Legend: ✅ done (parity-ish) · 🟡 partial/simplified · ❌ not started

## Engine / infrastructure
- ✅ Scene graph (Node/UINode/UIWidget), RefPtr tree, invalidation, hit-test, coordinate conv.
- ✅ Coordinator (SceneNode) + Actions/ActionManager + MutationQueue (deferred delete).
- ✅ Render seam (DrawContext over VG) + drawables (rect/rounded/border/gradient/image/nine-slice/statelist/layer).
- ✅ EventDispatcher: hover/click/focus + **pointer capture** (drag). gui.shell bridge (event + InputSurface poll).
- ✅ CSS engine: selector/specificity/cascade/.css parser/typed-apply/!important/vars/@media/transitions→Actions/live StyleManager+hot-reload.
- ✅ Focus: click-to-focus + RequestFocus/ReleaseFocus + **Tab / Shift+Tab traversal** (Node::IsTabFocusable,
  eepp-style). ❌ directional (arrow) focus navigation; click-to-focus opt-out flag (eepp DISABLE_CLICK_FOCUS).
- ✅ Input: mouse fully wired; **keyboard/text proven end-to-end** (TextField on screen). Focus-driven
  platform text-input (IME) generalized through the bridge: Node::WantsTextInput() + SetTextInputTarget.
- ✅ **Drag-and-drop** (DnD source/target): BeginDrag(payload) + SetDropAcceptor predicate +
  DragEnter/Over/Leave/Drop events, delivered to the nearest accepting ancestor of the cursor.
- ✅ Tooltips (hover-delay popup, TooltipManager).
- ❌ Clipboard, cursor-shape management (per-widget).

## Text  (biggest single gap vs eepp)
- ✅ Basic Text: single font, color, H/V alignment, measure, draw (baseline-correct).
- ✅ **Word wrap / multi-line** (greedy at whitespace + explicit '\n'; SetWordWrap / ComputeLines /
  MeasureWrapped). In-word breaking for over-long words is a follow-up.
- ❌ Rich text (per-range color/style), outline/shadow, **HarfBuzz shaping + bidi/RTL** (eepp's
  standout feature). draconic.fonts has a shaper to build on.

## Layouts
- ✅ LinearLayout (H/V + spacing), **GridLayout** (fixed columns, wrap, H/V spacing),
  **RelativeLayout** (parent edge/corner/center anchors via Anchor bitmask).
- ❌ TableLayout, FlexLayout, AbsoluteLayout. No measure/wrap-content, weights/stretch, gravity,
  stretch-to-cell, sibling-relative rules (children keep own sizes).

## Widgets (16 of ~95)
- ✅ Label, Button, CheckBox, Slider, ProgressBar, **TextField** (single-line editable: caret,
  UTF-8 insert/delete, arrow/home/end nav, click-to-place, blink; selection/clipboard/multi-line TODO),
  **ScrollBar** (grab-drag thumb + track paging, H/V), **ScrollView** (clip + offset + wheel +
  auto-managed overlay bars w/ two-way sync + policy + keyboard scroll + auto-measure content).
- ✅ **RadioButton/RadioGroup** (mutual exclusion), **Image** (drawable + scale modes),
  **ListBox** (scrollable single-selection), **TabWidget** (tab bar + panels), **Window**
  (drag/resize/raise), **ComboBox** (dropdown), **Menu/PopupMenu** (context menu), **Tooltip**
  (hover-delay). Popup/overlay support lives on the EventDispatcher (OpenPopup/ClosePopup +
  outside-click/Escape dismiss).
- ❌ SpinBox, Splitter, MenuBar, checkable/submenu Menu items, plus eepp's icon+text button composition.
- ❌ Heavy/optional: CodeEditor (+syntax), RichText/HTML/Markdown viewers, Terminal, Console.

## Theming / skins
- ✅ **CSS default theme** (Theme.cppm dark+light) applied via StyleManager; runtime theme switching
  (swap sheet). **Pseudo-elements** (`tag::part`) for widget parts (slider::fill, window::title, ...).
  Resource seams: **IResourceProvider::LoadImage → ImageData** (applier wraps ImageDrawable) +
  **fonts::IFontService** for font-family (reused, not reinvented). CSS `color`/`background-image`/
  `font-family`.
- ❌ Nine-patch skin sheets / named-skin resources, per-widget StateList skins from a theme file.

## MVC / data
- ❌ Model/View (list/table/tree **adapters**, virtualized item rendering) — eepp's models/ (4k) + abstract views.

## Markup / authoring
- ❌ **XML layout loading** (inflate a widget tree from markup) + CSS file loading/hot-reload from disk.
  (Have: CSS *string* parsing + StyleManager hot-reload API.)

## CSS remaining (all niceties)
- ✅ Resource-backed props (background-image via IResourceProvider::LoadImage→ImageData; font-family via
  IFontService). ✅ pseudo-elements (::part).
- ✅ @keyframes runtime (opacity; animation property + KeyframeAction). ❌ @import (needs LoadText), easing/timing functions,
  dirty-tracked re-resolve, partial/nested var(), variable inheritance, sibling combinators /
  :nth-child / attribute selectors.

## Known bugs
- ✅ FIXED: Slider drag cancelled when the cursor left the widget while held. Slider now has its
  own `m_dragging` flag (set in `OnMouseDown`, cleared in an `OnMouseUp` override, gated in
  `OnMouseMove` instead of `IsPressed()`), so the hover `leave` clearing `m_pressed` no longer
  stops the drag. Regression test hovers → presses → drags off. (Same pattern for future drag
  widgets: ScrollBar.)

## eepp subsystems intentionally out of scope (for now)
- Tools dir (widget inspector), doc/ code-editor stack, HarfBuzz vendoring, network-backed HTML.

---

## Suggested priority for next sessions
1. ✅ **DONE — app-usable**: TextField (keyboard/text end-to-end) · GridLayout + RelativeLayout ·
   ScrollView/ScrollBar (clip + capture-drag) · focus tab-navigation.
2. ✅ **DONE — breadth of controls**: Radio/RadioGroup · ComboBox/DropDownList · ListBox · Window (drag/resize) ·
   TabWidget · Menu · Tooltip · Image (+ EventDispatcher popup/overlay support).
3. **Fidelity** (in progress): ✅ text wrap · ✅ drag-and-drop · ✅ theme/skin (CSS default theme + pseudo-elements) ·
   ✅ CSS resource-backed props (IResourceProvider::LoadImage + IFontService) · ✅ @keyframes runtime · ✅ draconic.gui.vfs ·
   ⏭ NEXT: ❌ rich text (per-range color/style) · in-word text break.
4. **Data & authoring**: model/view (list/table/tree) · XML markup loading.
5. **Heavy/optional** (only if wanted): CodeEditor · HTML/Markdown · Terminal.

---

# DEEP GAP ASSESSMENT vs eepp (2026-07-09, post-Priority-3)

Two full eepp source surveys (widgets + CSS). We have 16 widgets + 3 layouts + a solid CSS engine;
eepp has ~55 widgets, CSS-box-model layout, a full MVC layer, and a ~250-property CSS spec. Below =
what we're missing / where ours is thinner, prioritized.

## Widgets we DON'T have yet (eepp UI* classes)
HIGH-VALUE / common:
- **SpinBox** (numeric field + up/down, min/max/step) — small, high utility.
- **Menu depth**: MenuBar + MenuItem variants — **submenus, separators, checkable/radio items, icons,
  shortcut column**. Our Menu is flat text-only. Biggest single-widget gap in breadth.
- **Splitter** (draggable 2-pane), **StackWidget** (single-visible child), **ViewPager** (paged).
- **Multi-line text**: **TextEdit** (multi-line editor) — we have NO multiline editing at all.
- **Dialogs**: **MessageBox** (modal alert/confirm), **FileDialog** — need modal Window support first.
- **Loader/spinner**, **SelectButton** (toggle button).
MODEL-BACKED (need the MVC layer first, see below): **ListView, TableView, TreeView, MultiModelView,
DropDownModelList**.
RICH/HEAVY (later/optional): RichText (styled runs), MarkdownView, CodeEditor(+syntax/LSP), WebView/HTML*
form controls, Console, Sprite/Svg/TextureRegion display, Icon/IconTheme, WidgetTable.

## Depth gaps in the widgets we DO have (thin vs eepp)
- **TextField** (biggest): no **selection, clipboard cut/copy/paste, undo/redo, hint/placeholder text,
  password mode, max-length, numeric filter, double-click word-select, context menu, IME editing events,
  multi-line**. This is the #1 fidelity gap.
- **Menu**: submenus/separators/checkable/radio/icons/shortcuts (see above).
- **ListBox**: NOT virtualized (recycled rows), single-select only (no multi-select), no horizontal scroll.
- **TabWidget**: no closable tabs/close button, no DnD reorder, no per-tab icons, no auto-hide bar,
  no min/max tab width, no try-close callback.
- **Window**: no **modal**, no minimize/maximize buttons, no decoration-skin config, no framebuffer render,
  no ephemeral (close-on-focus-loss), no keybinding/command system, no window opacity.
- **Slider/ScrollBar**: no **vertical mode**, no click-step/page-step model, no keyboard (arrows) on Slider,
  ScrollBar no 2-button arrow style.
- **Label (TextView)**: no text **selection/copy**, no per-range fill color, no text-transform, no clickable
  href links. (Word-wrap ✓.)
- **ProgressBar**: no animated fill, no built-in percent label, no vertical.
- **ScrollView**: no inertial/anchored scroll, no ViewType (inclusive/exclusive), no touch-drag.
- **ComboBox**: no editable-text mode, no keyboard nav, no max-visible-items.
- **Tooltip**: no word-wrap/shadow/transform styling (we set colors only).
- **CheckBox/RadioButton/Image**: roughly at parity (minor: keyboard toggle, icon modes).

## Layouts + MVC (whole subsystems missing)
- **Layout**: eepp layout is **CSS-box-model driven** (margins/padding/flex/table via BlockLayouter/
  FlexLayouter/TableLayouter/InlineLayouter). We have 3 fixed managers (Linear/Grid/Relative) with no
  measure/wrap-content, weights, gravity, flexbox, or table layout. Big architectural gap for real layouts.
- **MVC / models** (entirely absent): eepp `ui/models/` = Model base, ModelIndex/Role/Selection, Variant,
  SortingProxyModel, editing delegates, concrete models (FileSystem/ItemList/StringMap/WidgetTree), abstract
  views (UIAbstractView/TableView). Our ListBox is object-based, no sorting/filtering/editing/shared-model.
  Prereq for List/Table/Tree views.

## CSS engine gaps (ours vs eepp's ~250-property spec)
SELECTORS: **we lack** sibling combinators (`+` `~`), ALL attribute selectors (`[a=v]`,`^=`,`$=`,`*=`,`~=`,
`|=`), ALL structural pseudo (`:nth-child(an+b)`, `:first/last-child`, `:only-child`, `:empty`, `:checked`,
`:not()`, `:is()`, `:where()`, `:root`), and `:focus-within`/`:selected`/`:link`/`:visited`. (We have tag/id/
class/`:hover/:focus/:active/:disabled`/`*`/descendant/child/`::part`.)
PROPERTIES (we have ~15; eepp ~250): **we lack** border/border-radius/border-* (per-side, box-sizing),
full background (position/repeat/size/origin/clip/tint) + foreground-*, text (**text-align**/text-transform/
text-decoration/text-shadow/text-stroke/**font-style**/**font-weight**/line-height/letter-spacing/white-space/
text-overflow), transform (rotate/scale/origin/blend-mode), box (**min/max-width/height**, cursor, z-index,
position+top/left, overflow, display), **flexbox** (flex-direction/justify-content/align-*/flex-grow/gap/order),
grid. Also property **aliases** (bgcolor↔background-color).
AT-RULES/FUNCTIONS: **we lack** `@import` (needs LoadText on the provider), `@font-face`, richer `@media`
(orientation/aspect-ratio/**prefers-color-scheme**/resolution + `and`/`not`/`,`), functions `calc()`/`min()`/
`max()`, gradients (linear/radial), `rgb()`/`rgba()`/`hsl()`, multiple `var()` per value, and units
rem/em/vw/vh/vmin/vmax (we have px + unitless only).
TRANSITIONS/ANIMATIONS: transitions animate **opacity only** (eepp: all animatable types incl color/vector/
length); **no easing/timing-functions** (eepp: full Ease set + cubic-bezier); transition/animation shorthands
lack **delay, timing-function, property-lists, iteration-count(number), direction, fill-mode, play-state**.
@keyframes: we do opacity+background-color; eepp does any property per block. (Multi-property tracks = our
next keyframes step.)

## KNOWN BUG: @keyframes animation regressed (DraconicGUITests, found 2026-07-22)
`Code/Draconic/GUI/Tests/KeyframesTests.cpp` has 4 failing cases + 1 crash - the CSS `@keyframes` engine
is not driving values correctly:
- "the animation property drives opacity across the stops" - GetAlpha() wrong at every sampled time
  (:66 expects 0.5, :69 expects 0.0, :72 expects 0.5).
- "infinite animations loop" - GetAlpha() wrong (:92, :96 expect 0.0).
- "the animation is spawned once, not re-spawned each ApplyTree" - GetAlpha() wrong (:114 expects 0.0).
- "animates background-color across the stops" - **CRASHES (SIGSEGV)**: `REQUIRE(r != nullptr)` at :134
  fails (a resource resolves null) then segfaults.
Entirely inside draconic.gui - no scene/net/input dependency; fails IDENTICALLY on clang + gcc, so it is
a real GUI-engine regression (the animation sampler / keyframe-track resolution), not environment. Pre-
dates the 2026-07-22 networking/scene work (that touched none of draconic.gui). Fix the keyframe sampler +
the null-resource path; the opacity CHECKs are the cheapest repro to drive it.
CASCADE: **we lack** property **inheritance** (inherited vs not — color/font-* should inherit), **shorthand
expansion** (margin/padding/border/background/font/flex parsed as one), relative-length resolution (em/%/
font-size-relative), and finer specificity buckets (structural-pseudo/`:where()`=0/`:root`).

## Suggested next-session roadmap (by leverage)
1. **TextField depth** (selection + clipboard + hint + max-length) — highest-impact single widget.
2. **Menu depth** (submenus/separators/checkable/icons) + **MenuBar** — unlocks real app menus.
3. **CSS breadth batch**: `text-align`/`font-weight`/`font-style`, `border`/`border-radius`,
   `min/max-width/height`, shorthand expansion, `rgb()/rgba()`, more units — cheap, broad fidelity.
4. **Modal Window** → then **MessageBox** + **FileDialog**.
5. **MVC layer** (Model/ModelIndex/abstract view) → **ListView/TableView/TreeView** (replaces ListBox
   internals with virtualized model-backed views).
6. **XML markup loading** (inflate widget tree) — declarative UIs.
7. Flexbox layouter (CSS-box-model layout) — architectural, high effort.
8. Rich text (styled runs), then transitions-beyond-opacity + easing, SpinBox/Splitter/StackWidget.
