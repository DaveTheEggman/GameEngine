# Viewport Input - Design (v1)

Status: **DESIGN (2026-07-03)** - approved decisions locked; not implemented.

Goal: one pristine mechanism for routing input into a **content surface placed in a
window through a fit transform**, replacing the two ad-hoc paths Sedulous grew
(editor-embedded-game panel + runtime fit modes). Draconic is greenfield here - no
UI layer, no editor, no fit modes exist yet - so this designs from the substrate
(`IMouse` window-space, `IRenderWindowData`, `ViewportRect`) rather than untangling.

## 1. The problem is singular

The two features are the **same problem** wearing two hats:

- **Editor embedded game** - the game renders to an offscreen target and is shown in
  a panel; input must be restricted to the panel and remapped into game-content
  space, with focus so keyboard/gamepad reach the game only when the panel is active.
- **Runtime fit mode** - the game/UI renders at a design resolution and is placed
  into the window with a fit (letterbox/stretch/…); pointer input must be remapped
  through that fit, and letterbox bars must not hit content.

Both are: *a content surface at some region of a window, through a fit transform,
that owns focus/capture and remaps input into content space.* Sedulous built two
converging paths (`GameMouseAdapter` for the editor, `EngineCanvasMouseAdapter` for
standalone - cross-referenced in their own comments as "mirrors the … pattern"). We
build **one**.

## 2. What we learned from Sedulous

**The gem (keep):** `ViewportView.ComputeContentRect` + `ScreenToTexture` - draw and
hit-test derive from **one fit function**, and letterbox bars return "no hit." Also
worth keeping: the UI router's physical→logical DPI normalization at the boundary,
its ViewId-keyed hover/press/capture, and `IMouse.MouseHoverWindow` as the mouse
routing authority (distinct from keyboard focus).

**The smells (do NOT port):**
- **Fit-mode enum triplication** - `ViewportFitMode`, `RuntimeGraphics.FitMode`, and
  editor page `FitMode` kept aligned only by matching ordinals + casts. → **one** `FitMode`.
- **Keyboard/gamepad never reach the viewport** - the handler interface is mouse-only;
  the code comments "isn't wired here yet." → the surface gates **all** input kinds.
- **Two `IMouse` adapters** solving one problem twice (convergent evolution). → **one** surface.
- **Dead/unwired** fit-aware infra (`SetScreenToCanvas`, `EngineCanvasMouseAdapter`
  never called; `UIConsumedInput` never set true).
- **World-UI path bypasses the transform** (polls raw `mouse.X/Y`, ignores
  `coordTransform`) → two coordinate pipelines that disagree. → **single** source of truth.
- **Scattered gating** - "should this go to the game" decided three ways
  (hit-test-non-root, `IsMouseOverUI`, editor `IsRunning`) with no owner. → **one owner**.

## 3. Decisions (locked)

1. **Surface presents the same device interfaces, transformed (drop-in).** An
   `InputSurface` yields `IMouse`/`IKeyboard`/`IGamepad`/`ITouch` that are remapped +
   gated. A consumer (`FlyCamera`, ImGui feed) polls `surface->Mouse()->X()` instead
   of the raw manager and is otherwise unchanged. Maximal reuse; no per-consumer
   viewport code.
2. **Event-first, with a poll snapshot facade.** Events are the source of truth (a
   dispatch pipeline with capture/target/bubble + hover enter/leave, for the coming
   retained-mode UI). The device interfaces above are a **snapshot** the surface
   maintains *from* that event stream - so poll and event views never disagree
   (reconciles decisions 1 and 2: `surface->Mouse()->X()` reads the latest snapshot
   the events produced).
3. **Per-window input tagging + focus now.** Fix the discarded SDL `windowID` at the
   pump; input state is per-window; the manager exposes a hover-window and a
   focused-window authority. Foundation for the editor's floating/detached panels
   (separate windows, per `runtime-host.md` §6).

## 4. Architecture

```
OS events
  → SDL pump: tag each input event with windowID  (§4.1)
  → per-window device state + a per-window EVENT STREAM  (§4.2, source of truth)
  → InputRouter: focus/capture/hover ownership + dispatch order  (§4.4)
  → InputSurface: ContentFit transform + gate; maintains a polled snapshot  (§4.3)
  → consumers: poll surface->Mouse()/Keyboard()/… (drop-in), or receive events (UI)
```

### 4.1 Per-window input tagging (platform)

Today the SDL pump discards `event.*.windowID` for input (only lifecycle
`WindowEvent`s carry it) and binds the mouse to the main window alone. Change:

- Route each input event to the **per-window** device state keyed by its `windowID`.
- The manager exposes `HoverWindow()` (window under the pointer - the mouse routing
  authority, per Sedulous) and `FocusedWindow()` (keyboard/gamepad target window).
- `IMouse::X/Y` stay window-space **relative to that event's window** (unchanged
  contract, now correct per-window). Global/desktop coords available if needed.

Keep the raw, untransformed manager as-is for anything that legitimately wants
global input; surfaces are layered *on top*, not a replacement.

### 4.2 The event stream (source of truth)

A flat, tagged input event: `{ window, kind, … }` - mouse move/button/wheel,
key down/up + text, gamepad button/axis, touch. Deltas and text accumulate per
frame as today. The stream is what the router dispatches and what surface snapshots
are rebuilt from each frame. (This is the only *new* platform surface area; the
device interfaces already exist.)

### 4.3 `FitMode` + `ContentFit` - one source of truth for render AND input

```cpp
enum class FitMode { Stretch, Letterbox, Crop, IntegerScale };

struct ContentFit {                 // pure geometry value type, no owned state
    Rect    region;                 // outer rect the content is placed in (REGION-space)
    Vec2    contentSize;            // logical content resolution
    FitMode mode;
    // one computation, shared by renderer + input; names are space-neutral (no "window"):
    Rect DstRect() const;           // renderer blits here (region-space)
    Rect SrcRect() const;           // renderer samples this (Crop slices source)
    bool ToContent(Vec2 pt, Vec2& out) const;  // region-space -> content; false => outside (bar)
    Vec2 FromContent(Vec2 pt) const;           // content -> region-space
    Vec2 Scale() const;             // region -> content, for delta / relative-mouse
};
```

`region` is in **region-space** - the same coordinate space the caller works in; it
is NOT inherently a window. The `InputSurface` (runtime layer, §4.5) is what fills
`region` with a window rect and calls `ToContent(windowSpacePoint)`; the editor fills
it with a panel rect; a split-screen view with a sub-rect. Keeping the type
window-agnostic is why it belongs in core (the domain word "window" lives in the
surface, not the geometry).

- The **renderer/blit** uses `DstRect/SrcRect` to place the offscreen content.
- The **input path** uses `ToContent` (remap) + its `false` return (letterbox bar =
  no hit) + `Scale()` (scale mouse **delta** so look sensitivity is invariant to
  region size - a detail Sedulous never handled).
- One object → render placement and input remap **cannot drift**. This is Sedulous's
  gem promoted to a shared type, and it dissolves the triplication + the two-pipeline
  disagreement.

`FitMode` + `ContentFit` live in **`draconic.core` (math)**, next to `Rect`/`AABB`/
`Plane`/`BoundingVolumes` - they're pure dependency-free geometric value types (a
`Rect` + `Vec2` + enum → derived rects + a point transform), exactly the category
core already holds, and both consumers (graphics present/blit and the input surface)
already import core, so there's no new module, no cycle, and the future UI shares it
for free. (Putting it in `runtime.platform`/`runtime.graphics` would be a category
mismatch and would force input to depend on the GPU/OS layer for a value type.)
Likely `Core/Math/ContentFit.cppm`, or folded into the existing `Rect.cppm`. It
replaces `ViewportRect`'s render-only role for placed content (split-screen sub-rects
are `Stretch` fits).

### 4.4 Focus / capture / hover - the single owner

One `InputRouter` **per host, keyed by window** (a single keyboard/gamepad focus
across the whole app; mouse routed per-window via `HoverWindow()`) owns:

- **Keyboard/gamepad focus** - exactly one focused surface at a time. Set by
  click-into-surface or explicit `Focus()`. Unfocused surfaces report **no** key/pad
  input (closing Sedulous's gap where kb/pad never reached the viewport).
- **Mouse hover** - derived from the pointer position vs each surface's `DstRect`
  (in hover-window order). Drives which surface's mouse is "live" + cursor.
- **Mouse capture** - a surface that begins a drag holds capture until release, so
  drags that leave the rect still deliver to it.

This single owner replaces Sedulous's three scattered "does the game get it?"
mechanisms. "UI wants the mouse" becomes: the UI is a higher-priority surface (or the
`RootView` surface) that consumes the event before the game surface sees it.

#### 4.4.1 Overlay capture - interim vs generic (decided 2026-07-03)

**Shipped (interim):** `InputRouter::SetExternalCapture(bool mouse, bool keyboard)`.
While set, the router marks *no* surface hovered/focused, so viewport cameras ignore
what the overlay is using. The Sandbox feeds `ImGui::GetIO().WantCaptureMouse /
WantCaptureKeyboard` (valid after `ImGui::NewFrame`) into it before `router.Update()`.
This fixed "scrolling the ImGui debug window also zoomed the scene." It's the
**single-overlay special case** - a router-global flag, not a first-class stack entry.

**Generic target (build when the UI layer lands, not before):** a **surface stack with
z-order + occlusion** - the router routes each event to the *topmost* surface whose
hit-test contains the point; lower surfaces are occluded; a surface is input-opaque
(swallows) or pass-through. **Key reconciliation:** a surface's hit-test is *either* a
rect region (viewports) *or* a **predicate callback**. ImGui can't expose rects - its
capture is a black box (a drag continues to capture outside the widget's rect; popups;
keyboard focus in a text field; richer than any rectangle), so its *only* clean signal
is `WantCaptureMouse`. Therefore ImGui becomes a top surface whose predicate =
`WantCaptureMouse`, and `SetExternalCapture` is **subsumed** ("the topmost
predicate-surface contains the point") and deleted.

**Why defer:** building the stack now is speculative generality for one debug overlay +
two viewports, and ImGui would need the predicate bridge either way. The retained-mode
UI (Sedulous.UI port) is the thing that genuinely wants the stack (real hit-testing);
do it there. The flag is not a dead end - it is the interim 1-overlay case of that rule.

### 4.5 `InputSurface`

```cpp
class InputSurface {
    // identity + placement
    IWindow*   window;
    ContentFit fit;              // region + contentSize + mode (§4.3)
    // gate state (owned by InputRouter): hovered, focused, captured

    // drop-in device facade (snapshot rebuilt from the event stream each frame):
    IMouse*    Mouse();          // content-space; "not over" outside DstRect
    IKeyboard* Keyboard();       // only reports state while focused
    IGamepad*  Gamepad(i32);     // only while focused
    ITouch*    Touch();          // content-space

    // event path (for UI / handlers that want events, not polling):
    // receives dispatched events already transformed to content space + phase-tagged
};
```

- The facade `IMouse`/`IKeyboard`/`IGamepad`/`ITouch` are **thin views** over the
  surface's snapshot; a consumer written against the raw interfaces works unchanged.
- The event path carries transformed, phase-tagged events for the retained-mode UI
  (capture → target → bubble), so the UI never needs viewport awareness - the surface
  hands it content-space coordinates.

## 5. How the two use cases fall out

- **Runtime fit** - one surface per window: `fit.region = window rect`,
  `contentSize = design resolution`, `mode = runtime fit`. The game/UI polls it; the
  present blit uses the same `ContentFit`. Split-screen = N surfaces (Stretch fits at
  sub-rects), each pollable - so "which view is controlled" stops being a keyboard
  toggle and becomes *hover/focus of that view's surface*.
- **Editor embedded game** - a surface whose `fit.region = the ImGui/UI panel rect`,
  `contentSize = the game's render resolution`, focused when the panel is. The game's
  `Configure`d subsystems poll this surface (they already take an `IInputManager`-
  shaped thing). Detach to a floating window → the surface's `window` changes; per-
  window tagging (§4.1) makes that just work. **No `GameMouseAdapter`.**

## 6. Fit with `runtime-host.md` §6 (UI docking)

Consistent with "one UI context, N `RootView`s (per RenderWindow), shared
input/focus/dragdrop": each `RootView` is (or owns) a surface; `IRenderWindowData`
(already the intended `{RootView, VGContext, VGRenderer}` slot) also holds the
window's input-surface registry. The editor game panel is a `View` whose surface is
inside its RootView's dispatch order. **Chain-ready, not chain-now:** the priority
ordering hook exists; the full context-stack (Sedulous `InputContext` priorities)
lands with the UI, not before.

## 7. Non-goals / banked

- The full **action-mapping** layer (Sedulous `InputContext`/`InputAction`/bindings)
  is a separate, higher layer that consumes surfaces; not part of this design.
- The retained-mode **UI event router** (three-phase dispatch, ViewId hover/capture)
  is designed to sit on this but is built with the UI, not now.
- **IntegerScale** fit mode is included in the enum but may ship later than the others.

## 8. Phasing

1. **Platform:** per-window input tagging at the SDL pump + `HoverWindow()`/
   `FocusedWindow()` + the tagged event stream. (No new device interfaces.)
2. **Core:** `FitMode` + `ContentFit` (shared by graphics present + input).
3. **InputRouter + InputSurface:** focus/capture/hover ownership; the device-interface
   snapshot facade; the transformed event dispatch.
4. **Prove it:** convert Sandbox split-screen + `FlyCamera` to surfaces (each view a
   surface; hover/focus selects the controlled view) and route the ImGui feed through
   a full-window surface (add the missing `WantCaptureMouse`-style arbitration for
   free - ImGui becomes a higher-priority surface).
5. **Later, with the UI/editor:** RootView surfaces, the editor game panel, floating
   windows, the priority chain.

## 9. Open questions

- ~~Router scope?~~ **RESOLVED: one `InputRouter` per host, keyed by window** (§4.4) -
  single keyboard/gamepad focus across the app; mouse per-window via `HoverWindow()`.
- ~~Where does `ContentFit` live?~~ **RESOLVED: `draconic.core` math** (next to `Rect`;
  see §4.3) - pure value type, both consumers already import core.
- ~~Text input + IME?~~ **RESOLVED (design note):** take real SDL `SDL_EVENT_TEXT_INPUT`
  into the stream - not keycode emulation (Sedulous's `UIInputHelper.KeyToChar`). SDL3
  text input is **per-window and opt-in**: `SDL_StartTextInput(window)` /
  `SDL_StopTextInput(window)` must gate it, so it is **focus-driven** - the router
  starts text input on the focused element's window when a text-consuming element gains
  focus and stops it otherwise (always-on would enable IME composition that swallows
  game keybindings and pop the mobile on-screen keyboard). IME candidate placement
  (`SDL_SetTextInputArea(window, caretRect, …)`) needs the caret in WINDOW space, so a
  text field inside a fitted/panel surface maps its content-space caret out via
  `ContentFit::FromContent` - the concrete justification for that inverse.
