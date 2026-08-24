# Docking v2 - floating windows, previews, and the Linux chrome ruling

**Status:** DIRECTION SET (user + Fable, 2026-08-24), from PaperKid editor-use
feedback. The stuck-drag WATCHDOG shipped with this doc; the rest is scheduled
work, not started.

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

### What the chromed mode requires (the work item)

- **Panel chrome suppression**: DockablePanel hides its own title bar when its
  host window is OS-chromed (double title bars otherwise); the panel TITLE
  propagates to `WindowSettings.title` (today every float is titled "Panel").
- **Close routing**: the OS close button must reach `onCloseRequested` - the
  shell needs a per-window close-requested event (only the MAIN window has one
  today).
- **Re-dock gesture**: with the OS moving the window, re-docking cannot ride
  the app-drag path. Two affordances: (a) dragging the panel's TAB (already an
  in-window drag through the drag-drop system - works unchanged) re-docks; (b)
  OS window-move events show the dock zones when a dragged float overlaps the
  main window - investigate Wayland's position semantics during interactive
  moves before promising (b); (a) alone is acceptable v1.
- The app-driven move path (borderless mode) stays for Windows - unchanged.

## Drop/drag preview improvements (all platforms)

- **Zone-hover dock preview**: hovering a DockZoneIndicator shows a translucent
  RECT overlay of the would-be dock area (the standard docking UX everywhere) -
  the missing piece the user called out.
- **A real drag visual**: replace the small icon with a tab-shaped preview
  carrying the panel title (sized like the tab, anchored at the cursor).

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

## Sequencing

The watchdog is in. Everything else is one editor-polish work item (chrome
flip + close routing + panel title + zone-rect preview + drag visual + the
render investigation), scheduled via the weekly - it should NOT ride the
terrain track. The re-dock-via-tab affordance keeps v1 honest if Wayland move
events prove unhelpful.
