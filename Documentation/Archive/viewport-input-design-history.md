# Viewport Input - design history (archived)

> Status: ARCHIVED
> Superseded by: Documentation/Systems/viewport-input.md
> Track: [[viewport-input]]

NON-AUTHORITATIVE. The Sedulous reference read behind the 2026-07 viewport-input layer. Present-tense
truth is `Systems/viewport-input.md`; the full original design (v1, all four decisions + the per-section
architecture) is in git at the P0 commit 3b92560d. Kept for the "why".

## The gem (kept)

`ViewportView.ComputeContentRect` + `ScreenToTexture` - draw and hit-test derive from ONE fit function,
and letterbox bars return "no hit." Also kept: the UI router's physical->logical DPI normalization at
the boundary, its ViewId-keyed hover/press/capture, and `IMouse.MouseHoverWindow` as the mouse routing
authority (distinct from keyboard focus).

## The smells (NOT ported)

- **Fit-mode enum triplication** (`ViewportFitMode`, `RuntimeGraphics.FitMode`, editor `FitMode` kept
  aligned only by matching ordinals + casts) -> ONE `FitMode`.
- **Keyboard/gamepad never reach the viewport** (the handler interface is mouse-only; "isn't wired here
  yet") -> the surface gates ALL input kinds.
- **Two `IMouse` adapters** solving one problem twice (`GameMouseAdapter` + `EngineCanvasMouseAdapter`,
  convergent evolution) -> ONE surface.
- **Dead/unwired** fit-aware infra (`SetScreenToCanvas`, `EngineCanvasMouseAdapter` never called;
  `UIConsumedInput` never set true).
- **World-UI path bypasses the transform** (polls raw `mouse.X/Y`, ignores `coordTransform`) -> a SINGLE
  coordinate pipeline.
- **Scattered gating** ("should this go to the game" decided three ways - hit-test-non-root,
  `IsMouseOverUI`, editor `IsRunning` - with no owner) -> ONE owner (`InputRouter`).

## The four locked decisions (all shipped)

1. The surface presents the same device interfaces, transformed (drop-in): an `InputSurface` yields
   `IMouse`/`IKeyboard`/`IGamepad`/`ITouch` remapped + gated, so a consumer polls `surface->Mouse()->X()`
   and is otherwise unchanged.
2. Event-first, with a poll snapshot facade: events are the source of truth; the device interfaces are a
   snapshot maintained from that stream, so poll and event views never disagree.
3. Per-window input tagging + focus: fix the discarded SDL `windowID` at the pump; per-window state; a
   hover-window + focused-window authority (the foundation for detached editor panels).
4. One fit function for draw + hit-test; letterbox bars are non-interactive.
