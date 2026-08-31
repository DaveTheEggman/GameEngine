# Docking v2 - floating windows, previews, and the Linux chrome ruling

> ARCHIVED 2026-09-01: everything still open (the secondary-window render bug,
> the Wayland verify, the optional extras) moved to
> Documentation/Plans/week-2026-09-05.md ("Backlog absorbed from docking-v2.md").

**Status:** BUILT (Fable, 2026-08-24). The stuck-drag watchdog, the Linux
OS-chrome flip, the zone-hover drop preview, and the tab-chip drag visual are
all SHIPPED; remaining open items are listed at the bottom. Direction set by
the user from PaperKid editor-use feedback.

## The problems (user, PaperKid dogfooding)

1. Dragging an undocked window shows only a SMALL drag-preview icon drawn in
   the main window - weak affordance for "you are moving this panel".
2. Drop preview is poor: hovering a dock-zone indicator does not show the
   would-be dock RECT, so you cannot see what the drop will do before doing it.
3. Stuck drag (intermittent): release the mouse and the floated window keeps
   following the cursor.
4. A detached window's VIEWPORT content does not draw unless the main window
   is visible (may be editor-side scheduling, not the windowing layer).
5. Borderless floats behave badly on Linux: Wayland dislikes app-positioned
   borderless windows, and X-on-Wayland (XWayland) blocks dragging across
   monitors. Borderless only really works on true X11 (and Windows).

## Architecture today (why these happen)

Floats are BORDERLESS OS windows (`RuntimeDockableWindowHost` sets
`ws.borderless = true`); the DockablePanel draws its own title bar and the APP
moves the window from mouse deltas (`MoveDockableWindow`). The drag ride is the
generic UI drag-drop system - `DockDragPreview` is the small drag visual (drawn
in the MAIN window's drag layer), `DockZoneIndicator` shows the zones but no
target-rect overlay exists. Cross-window drags are routed to the main window by
UIHost (the float chases the cursor, so the hovered window is the float
itself). App-driven moves are exactly what Wayland's model punishes: missed
releases (problem 3), no cross-monitor coordinates under XWayland (problem 5).

## RULING (user, 2026-08-24): Linux defaults to OS-CHROMED float windows

Borderless floats stay the default on WINDOWS (they work, and the custom
chrome looks better there). On LINUX the default flips to OS-decorated
windows - no true-X11 detection matrix (not worth the surface); one platform
default, overridable by a settings flag later if anyone asks. The OS then owns
move/resize/snap/cross-monitor, which structurally retires the app-driven-drag
failure class on Linux.

### What the chromed mode does (SHIPPED)

- **Policy**: `IDockableWindowHost::UsesOSChrome()` (default false);
  `RuntimeDockableWindowHost` resolves it from the main window's
  `WindowSystem` via `PrefersOSChromedDockables` (X11/Wayland -> true,
  everything else false; `SetOSChromeOverride` for tests / a future setting).
  `DockManager::FloatPanel` stamps `DockableWindow::HasOSChrome` from it.
- **Title propagation**: the float's `WindowSettings.title` = the panel title
  (was a literal "Panel" on every window).
- **Panel header STAYS as the re-dock handle** (deliberate divergence from the
  original "suppress the header" sketch): with the header gone there is no
  in-window drag source, and OS-window-move re-dock is not feasible on Wayland
  (the compositor holds the pointer grab - our main window sees nothing during
  an OS move). So the header remains - drag it over the main window to re-dock,
  double-click it to re-dock in place - but its close X is suppressed (the OS
  close button owns closing) and the inner resize-edge zones are disabled (the
  OS border resizes).
- **No cursor-chasing**: under chrome, `RuntimeDockableWindowHost::Tick` skips
  the drag-follow entirely - during a re-dock drag the float stays where the OS
  put it, and the tab-chip adorner + zone drop-preview show the in-flight
  state. This retires the app-driven-move failure class (the Wayland one) on
  Linux structurally.
- **Close routing**: the shell already posts per-window
  `WindowEventType::CloseRequested`; `RuntimeDockableWindowHost::Tick` now
  dispatches those to the matching entry's `onCloseRequested`, and the toolkit
  routes it through `DockablePanel::RequestClose` - so the dirty-page close
  INTERCEPTOR applies to the OS close button exactly like the drawn X.
- The app-driven move path (borderless mode) is unchanged for Windows.

## Drop/drag preview improvements (all platforms, SHIPPED)

- **Zone-hover dock preview**: `DockTarget` carries a `PreviewRect` (the region
  the panel would occupy - halves for edge splits at the 0.5 default ratio, the
  full node for Center/tab); the indicator draws the hovered target's rect as a
  translucent accent overlay under the zone chips.
- **Tab-chip drag visual**: `DockDragPreview` is now a compact 160x26
  cursor-anchored title chip with an accent underline (was a 200x120 mini
  window that read as a stray icon). Under OS chrome it also serves as the
  in-flight visual for float re-dock drags (the window no longer follows).

## Stuck drag - watchdog SHIPPED with this doc

Root cause: the release event can be lost in the float-chases-cursor handoff
(Wayland especially). UIHost::Update now runs a watchdog: an active drag with
the SHELL's global left button up for a full frame delivers the release at the
cursor through the normal routing, then hard-cancels if still latched. This
covers every platform and every in-window drag; the Linux chrome flip
additionally retires the app-driven-move class entirely. (Not unit-testable
headless - the Null shell's mouse is not scriptable and the trigger is OS event
loss; the negative path runs every frame in every UI test, and the user's
repro is the verification.)

## Secondary-window viewport render (investigation item)

The runtime host renders ALL windows uniformly every frame (verified -
windows[0] is not special in the loop), so the "detached viewport only draws
when the main window is visible" symptom is NOT the window loop. Suspects, in
order: the editor page's offscreen scene render scheduling (does it render only
during its host window's frame, and is the host rebind correct after a float?),
present/acquire behavior when the primary is occluded, and the UIHost
single-window input/update routing starving the float's layout. Needs a live
session; keep it paired with the weekly's existing entry.

## Remaining open

- **Secondary-window viewport render** (the investigation above) - needs a
  live session.
- **User visual verification** of the chromed flow on Wayland: float a panel
  (OS title bar + working close/resize/snap), re-dock via header drag (chip +
  drop preview, window stays put) and via header double-click, dirty-page veto
  on the OS close button.
- Optional later: dock-zone hints from OS window-move events where the
  platform reports positions during interactive moves (not Wayland); a user
  setting over `SetOSChromeOverride` if anyone wants borderless floats on
  Linux back.

Coverage: UI.Toolkit.Tests (chrome propagation + adorner/resize behavior, OS
close veto routing, preview rects) + the new UI.Application.Tests (platform
policy mapping).
