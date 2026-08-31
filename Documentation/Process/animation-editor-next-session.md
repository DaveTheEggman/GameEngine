# Continuation prompt - property-animation in-scene editor (P1b onward)

Paste this to resume. You are continuing the build of the in-scene property-animation
EDITOR for this C++ engine (Raptor, port of Sedulous; runtime + data model already done).

## Read first (binding)
- `Documentation/Specs/property-animation-editor.md` - APPROVED WITH AMENDMENTS by
  Fable. The rulings on the 5 open questions AND amendments A1-A10 at the bottom are
  BINDING. Follow the phase order.
- `Documentation/Specs/property-animation.md` - the runtime/data-model spec + the
  review-pass-10 required-fix list (the RUNTIME fixes #1-4,#7 are DONE + committed;
  editor items live in the editor spec now).
- `Documentation/Specs/asset-picker-slot.md` - a SEPARATE track. FABLE IS BUILDING IT
  in `Code/Editor/Editor.App`. Do NOT touch Editor.App / Editor.App.Tests - their
  territory. It provides the "Edit"-opens-the-panel activation path later.

## Done (committed, green clang + gcc)
- Runtime correctness (review pass 10 #1 script-facade register, #2 empty-channel
  no-teleport via `SampleMerged`, #3 kind/type mismatch disable-with-warning, #4
  Transform is animatable, #7 wire hardening + dead clip.loop removed).
- Inspector clip picker; curve tangent handles persist after mouse-up.
- **P1a (5b8b0d68)**: `Timeline` widget in `foundation.ui.toolkit`
  (`Code/Foundation/UI.Toolkit/Timeline.cppm`) - domain-agnostic, headless-tested.
  API: `SetDuration/Duration`, `SetPlayheadTime/PlayheadTime` (clamped, fires event on
  change), `SetPixelsPerSecond/PixelsPerSecond`, `SetScrollSeconds/ScrollSeconds`,
  `LabelColumnWidth`, `DisplayFps`, `TimeToX/XToTime`, `static PickTickStep`,
  `Event<void(f32)> OnPlayheadMoved`. Seconds-only (A3). `Color::Rgb(u8,u8,u8,u8=255)`
  added to foundation.core.
- **P1b (ba8860c6)**: retired the tool-mode AND the standalone clip page (A1);
  `PropertyAnimationPanel` (`:panel`, a `ui::FlexLayout` + `IClipEditorHost`) docked in
  a resizable vertical `SplitView` BELOW THE VIEWPORT ONLY (A2), scene-page-owned. Fixes
  #5 (view never recreated mid-edit) + #6 (re-snapshot on track-identity change; has a
  regression test). Timeline is the scrubber; numeric scrub retired from ClipEditorView
  (`SetScrubTime`). Collapse toggle in the header. Scene page Tick()s + DrawOverlay()s
  it. The primary module unit is now just the plugin registrar (asset types + creator);
  the H1 tool-panel seam is parked/unused.
- **P1c (0c827675)**: transport - Play/Pause/Stop + editor-local loop toggle; Play
  auto-advances the playhead by dt each frame (`Tick(f32 dt, bool editingLocked)`),
  driving Timeline + preview; loop wraps, loop-off clamps+stops, Stop rewinds. D6
  Editing|Playing ownership (active-play scrubs ignored; paused/editing scrubs reposition
  the clock). A6 damage gate (only active playback self-invalidates). Host tests A8.

P1 IS COMPLETE. Panel API for reference: ctor `(EditorContext&, scene::Scene&,
EditorCommandStack&, Selection<Guid>&)`; `Tick(dt, editingLocked)`, `DrawOverlay(dd)`,
`Play/TogglePause/Stop/SetLooping/IsPlaying/IsPaused/IsLooping/PlayheadTime`,
`NewClip/LoadClip/SaveClip/AddTracksFromSelection/View()/SetCollapsed/IsCollapsed`,
IClipEditorHost `Clip/Commands/MarkClipDirty/OnScrubTimeChanged/OnClipViewRebuilt`.
Note: the panel method is `EditorCtx()` (NOT `Context()` - that would shadow
ui::View::Context); `Transform` must be spelled `core::Transform` in the impl (ambiguous
once ui.toolkit is imported).

## NEXT: P1 REVIEW GATE, then P2
P1 is done and awaiting Fable review (phasing: P2 starts AFTER Fable reviews P1). One
deferral to raise with Fable: A4's per-instance loop default (seed the loop toggle from
the SELECTED entity's bound PropertyAnimatorComponent.loopMode) is NOT done - it would
couple the clip editor to engine.animation (the component lives there); the toggle is
editor-local defaulting to Loop for now. Decide: accept editor-local, or take the dep.

## Then P2 (after Fable reviews P1)
Grow `Timeline` into the dopesheet: lanes + keyframe diamonds + box-select + drags-are-
VISUAL + one atomic commit that RETURNS THE SORT PERMUTATION the host maps selection
through (ruling 1/D3/D4 - no id field on Curve/CurveKey; undo/redo reselect by time-
epsilon). Migrate `CurveCanvas` off its normalized 0..1 time to CONSUME the shared
Timeline seconds<->pixels transform (ruling 2 - kills the re-domain drift). Mode toggle
Dopesheet|Curves sharing the D1 transform. Two snap toggles + relative phase. Keyboard
map A10 (Space, Home/End, Left/Right + Shift 0.25x, Ctrl+Left/Right, K/Insert, Delete,
Ctrl+C/X/V, Ctrl+A).

## Key facts / conventions
- Data: `PropertyAnimationClip{f32 duration; Array<PropertyTrack> tracks}`;
  `PropertyTrack{String componentType,propertyPath; TrackValueKind kind;
  Curve channels[kMaxChannels]; Array<QuatKey> quatKeys}`; `CurveKey{time,value,
  tangentIn,tangentOut; CurveKeyInterpolation Constant/Linear/Cubic}`. `SampleMerged(t,
  current)` keeps live value for empty channels. Runtime "Transform" target = reserved
  component name bound to `core::Transform` via Set/GetLocalTransform.
- Toolkit widget conventions: `RTTI_OBJECT(Type, View)` (UNQUALIFIED base) + `RTTI_DEFINE_OBJECT`;
  global fragment `#include "Core/Prelude.h"` + `#include "Core/Reflection/Reflect.h"`;
  `ResolveStyleColor(StyleProperty::Background/BorderColor/TextDimColor/AccentColor/
  ErrorColor, core::Color::Rgb(...) fallback)`; `OnDraw(UIDrawContext& ctx)` ->
  `ctx.VG().FillRect/DrawText`, `ctx.FontService()->GetFont(px)`; `OnMouseDown/Move/Up
  (MouseEventArgs&)` (`e.Button==MouseButton::Left`, `e.X/e.Y`, `e.Handled`,
  `Context->GetFocusManager()->SetCapture/ReleaseCapture(this)`); `OnMouseWheel
  (MouseWheelEventArgs&)` (`e.DeltaY`); `Invalidate()`/`MarkNeedsRedraw()`.
  UI.Toolkit is one module `foundation.ui.toolkit` with per-widget partitions; add
  `export import :name;` to `ToolkitModule.cppm` + the `.cppm` to `CMakeLists.txt`
  FILE_SET; tests in `UI.Toolkit.Tests` (add `.cpp` to its add_executable). No braced
  `{...}` init-list in range-for (needs <initializer_list>); use a named C array.

## Standing rules (do not relearn)
- master only; NO Co-Authored-By trailer; NO em/en-dash (ASCII hyphen only); NO brand
  ("draconic" only in the `DRACONIC_` macro prefix + `DraconicTestMain.h` include).
- Build `-j4`, ONE target per `cmake --build`; never `rm -rf Bin/`. Reconfigure with
  `cmake -S . -B build/clang` after adding files. Verify clang (`build/clang` ->
  `Bin/Debug/Linux64-Clang/<T>`) AND gcc (`build/gcc` -> `Bin/Debug/Linux64-GCC/<T>`).
  RUN THE DEBUG BINARY (a stale `Bin/Release/...` will silently show old counts).
- Fable builds CONCURRENTLY in the same tree - stage EXPLICIT paths only, never
  `git add -A`; run `git diff --cached` before commit; use `git commit -F <file>` for
  messages (backticks in `-m` trigger shell substitution). Never touch Editor.App /
  Editor.App.Tests (Fable's asset-slot). Concurrent builds can corrupt a `.pcm`
  ("file too small to contain magic") - rebuild that module target + retry.
- Tests land with every addition; Pipeline stays UI-free; heavy 3rd-party headers in
  impl units. I do NOT invoke Fable - put design questions in the spec; the user relays.
- Reference editors farmed: Godot `/home/robert/Dev/CPP/godot/editor/animation/`,
  Traktor `/home/robert/Dev/CPP/traktor/code/Ui/Sequencer` + `Theater/Editor`, Sedulous
  `/home/robert/Dev/Beef/SedulousEngine/.../Pages/TimelineView.bf`. Findings are in the
  editor spec's reference survey.

## Housekeeping
Nothing is pushed since `dc38d295`. Ask the user before pushing (Fable's commits are
interleaved). Current branch tip after P1a: `5b8b0d68`.
