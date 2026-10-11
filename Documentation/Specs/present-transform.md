# Present transform - a landscape game on a portrait screen

> Status: DRAFT for review (2026-10-11). No code yet.
> Track: 0.1 backlog, the web demos on a phone ("Orientation on mobile").
> Mirror: Sedulous ports each phase after it lands.

## The problem

A phone held upright gives the page, and so the canvas, a portrait shape. Every sample game is
landscape, so held upright it renders squeezed into a tall narrow view, its touch buttons in odd
places. Turned sideways with auto-rotate on, everything already works, with no engine involvement:
the browser lays the page out again in landscape, the page's resize handler sizes the canvas's
backing store to the new shape, the web shell sees the size change and the swapchain is
reconfigured, and touch positions arrive in the turned page's frame. The system compositor turns the
pixels onto the glass. The engine only ever sees a wider window.

What does not work is the other case: the page stays portrait (the phone held upright, or its
auto-rotate off) while the game wants landscape. Then the engine must present its image turned a
quarter and turn input back, the job a native Android app gives the swapchain's pre-transform
(Vulkan `VkSurfaceTransformFlagBitsKHR`). A browser has no such surface transform, so the engine
does the equivalent itself. Designed as one abstraction, the same piece later maps onto Vulkan's
pre-transform for a native Android player.

## Goals

- A game declares its orientation; on a window of the other shape, it renders at its own shape and
  is presented turned a quarter, filling the screen sideways.
- Every pointer and touch position, every touch region, the UI's layout and its hit testing work in
  the game's frame, as if the window were the game's shape. Nothing in a game changes.
- Where the browser can lock the orientation, the page asks it to, and then nothing turns.
- No cost when nothing turns (desktop, a matching window): the path stays as it is today.

## Not in scope

- A half turn (180): no host asks for it; the type allows it, nothing chooses it.
- Native Android (a Vulkan pre-transform): later, on the same type (see "Native, later").
- Rotating the editor: its Game tab gains a preview of a portrait phone at most (an open question).
- Per-scene orientation: it is a game's, like its render resolution.

## What exists (facts, 2026-10-11)

- **The present path.** `ApplicationHost::Tick` acquires the backbuffer (`RenderWindow::BeginFrame`,
  `FrameContext{width, height, backbufferView}`); `DefaultApplication::RenderFrame` renders the
  scene straight into it. With a render resolution set (`ProjectSettings::renderWidth/renderHeight`,
  `renderFit`) it builds a `ContentFit` over the backbuffer and passes `SceneSize::FromFit(fit)`:
  the pipeline then draws the scene at that size into a transient target and a **present copy** (the
  tonemap pass in copy mode, `PipelineImpl.cpp` ~2462) samples it into the fitted rectangle of the
  real target. Screen overlays (`RenderSubsystem::RenderOverlays`: the UI's screen tier, ImGui) draw
  after it, at the backbuffer's full size.
- **`ContentFit`** (`Core/Math/ContentFit.cppm`): a region (target space), a content size and a
  `FitMode` (Stretch, Letterbox, Crop, IntegerScale), giving `DstRect`, `SrcRect`, `ToContent`,
  `FromContent`, `Scale`. Axis-aligned. Its users: the present copy (`SceneSize::FromFit`), the
  player's input (`FittedInputSource`: mouse, deltas, touches), the editor's Game tab input
  (`InputSurface`, `InputRouter`) and the UI's screen tier (`UISubsystem::ScreenFit`: layout at the
  resolution, drawn into `DstRect`, pointer points through `ToContent`).
- **The swapchain** (`RHI SwapChainDesc`) has no transform; Vulkan passes `preTransform =
  currentTransform` through; nothing rotates anywhere.

So every place that maps between the window and the game's frame already goes through `ContentFit`.
Teaching it a rotation teaches all of them.

## Design

### 1. `ContentFit` gains a rotation

```cpp
enum class ContentRotation : u8 { None, Quarter, Half, ThreeQuarter }; // content turned clockwise
struct ContentFit { Rectangle region; Float2 contentSize; FitMode mode; ContentRotation rotation; };
```

The content is fitted into the region as if the region were turned back: with a quarter turn, a 1080
x 1920 portrait region fits 1920 x 1080 content exactly. `DstRect` stays the axis-aligned rectangle
the content covers on the target; `ToContent`/`FromContent` turn points through it; `Scale` returns
the per-axis factor in the content's frame (a quarter turn swaps the axes); a new `RotateVector`
turns a delta (mouse motion). `None` is today's behaviour, bit for bit.

### 2. A game declares its orientation

`ProjectSettings::orientation : ScreenOrientation { Any, Landscape, Portrait }` (not `Orientation`,
the UI's layout direction), appended (a manifest saved before it reads `Any`), reflected into
Project Settings' Display tab, and overridable per export preset like the render size (a desktop
preset keeps `Any`; a phone-facing Web preset says `Landscape`). `Any` is today's behaviour: the
game takes the window's shape.

The host chooses the rotation each frame: `Landscape` on a window taller than wide, or `Portrait` on
one wider than tall, gets a quarter turn; otherwise none. The direction is the platform's primary
landscape (the phone turned counter-clockwise, Android's `landscape-primary`): the game's top faces
the screen's left edge. (Open question 1.)

### 3. The present turns the image

When the rotation is not `None`, the host always renders through the scaled path, even without a
render resolution: the content size is the render resolution when set, else the window's size with
its axes swapped (so the game renders at the landscape shape of the same pixels). `SceneSize`
carries the rotation (`FromFit`), and the present copy turns its sampling: in copy mode the tonemap
shader swizzles its local UV by the rotation before `UvScale`/`UvOffset` (a push-constant field; the
pass already has the slot for a scaled view's present copy). The scene-tier UI (HUDs on canvases)
draws inside the compose, so it turns with the image for free.

### 4. Input turns back

The host uses `FittedInputSource` whenever the fit is not plain (a render resolution, or a
rotation), not only for a render resolution. Through the rotated `ContentFit`: the mouse's position
and the fingers' `pointerX/Y` map into the content's pixels, the fingers' normalized `x/y` over the
content (so touch regions are in the game's frame), and mouse deltas turn with `RotateVector` (a
look speed keeps its direction). The editor's `InputSurface` and `InputRouter` follow from the same
`ContentFit` methods.

### 5. The screen-tier UI draws turned

The screen tier (pushed screens, the overlays, the touch guide) lays out at the content size, as it
does with a render resolution, and `DrawTier` draws its root with the rotation: a VG transform
(translate to the destination's corner, rotate a quarter) before `DrawRootInPass`. Its pointer
mapping (`ScreenPointerPoint`, `ScreenLayoutPoint`) already goes through `ToContent`. The touch
guide is a screen-tier view in the content's frame, so its buttons land in the game's bottom-right
corner whatever the phone does. ImGui (debug only) stays unturned.

### 6. The web page asks for the lock where it can

The loading card's "Play" (a tap, which a browser requires) requests fullscreen and, for a game that
declares an orientation, `screen.orientation.lock("landscape")`. Where the lock is granted (Android
Chrome), the page turns and the window is landscape: no rotation. Where it is refused (Safari on
iPhone, a desktop browser), section 3 turns the image. The page reads the orientation from
player.xml, as it reads the name. (Open question 3: whether to ask at all.)

### 7. Native, later

A native Android player reads the surface's `currentTransform`: a quarter turn there becomes
`ContentRotation::Quarter` for the present copy and input, the swapchain is created with
`preTransform = currentTransform` and its extent swapped, and the compositor does no work. Same
type, same paths; only the source of the rotation differs (the surface, rather than the orientation
setting against the window's shape). Noted here so the type is right; not built now.

## Phases

- **P1 `ContentFit` rotation.** The enum, the four methods with the rotation, `RotateVector`. Tests
  (Core.Tests): a quarter turn fits a portrait region exactly; `ToContent` / `FromContent` round
  trip at the corners and centre for each rotation and each `FitMode`; `Scale` swaps; `None` is
  byte-identical to today.
- **P2 The setting and the present.** `ProjectSettings::orientation` (+ preset override, the dialog,
  player.xml); `SceneSize` rotation; the tonemap copy swizzle; the host's choice. Tests:
  Engine.Project (round trip, an old manifest reads `Any`); Render.Tests (a scaled view with a
  quarter turn sets the copy's rotation and swaps the destination); a pixel probe (RHI.TestSupport
  harness: a known gradient presented with a quarter turn lands turned, on Vulkan and WebGPU).
- **P3 Input.** `FittedInputSource` whenever the fit is not plain; deltas turned. Tests
  (Engine.Input.Tests): a portrait window with a landscape game: a mouse and a finger at the
  window's corners map to the content's corners, a finger's normalized place is over the content, a
  drag's delta turns.
- **P4 The screen-tier UI.** `DrawTier` rotation. Tests (Engine.UI.Tests): with a quarter turn, a
  tap at the window point where a button is drawn presses it; the touch guide's buttons are in the
  content's bottom-right.
- **P5 The page and the samples.** The fullscreen and lock request on Play; the four samples' Web
  presets say `Landscape`. The user's phone test, held upright and turned, auto-rotate on and off.

## Open questions

1. **Direction of the quarter turn** when the page is portrait: counter-clockwise (the game's top on
   the screen's left, Android's primary landscape), or follow the device's sensor
   (`screen.orientation.angle` when it changes, needing a sensor the browser does not expose while
   the page itself is locked portrait)? Proposed: fixed counter-clockwise.
2. **The editor.** Should the Game tab offer a "phone, portrait" preview that exercises the turn (a
   tall Game tab with a landscape game)? Proposed: yes, as P4's manual check, since it costs only
   the host's choice running in the Game tab.
3. **The lock request.** Fullscreen on Play hides the browser's bars, which suits a game but
   surprises on a demos page with other content. Proposed: ask on Play only for a game that declares
   an orientation, and only on a touch device.
4. **`Any` as the default** keeps every project as it is; the samples opt in through their Web
   presets. A new project's template could default to `Landscape` on its Web preset.
