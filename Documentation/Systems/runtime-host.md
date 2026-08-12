# Runtime Host

> Status: CURRENT
> Verified: 2026-08-12 @ 42273d13
> Track: [[runtime-and-engine-buildout]] / [[runtime-host-v3]]

The host layer: a single `IApplication` IS the app/game; a generic `ApplicationHost` drives it over a
shared GPU device + N windows; the editor embeds the SAME application against a persistent runtime
context. Shipped (multi-window platform + graphics host + the inverted single-application model + the
embedded host). Run ownership (scenes + script brain) lives one layer up in `GameInstance` - see
[[game-instance-track]].

## The inversion

`IApplication` (`foundation.runtime.client`, `Application.cppm`) IS the app - exactly ONE per host. It
owns subsystem registration in `Configure(host)` (the host forces nothing in), so the subsystem set is
the app's alone and identical standalone or embedded. Hooks: `Settings`, `Configure`, `OnStartup`,
`OnLaunch`, `OnUpdate`, `OnFixedUpdate`, `OnRenderWindow`, `OnExit`, `OnShutdown`. This collapsed the
old EngineApplication-vs-client split and the composable-module-list model (there is no
`IApplicationModule`; behavior lives in the one `IApplication`).

`DefaultApplication` (`engine.defaultapp`) is the conventional `IApplication`: it registers all the
standard engine subsystems and owns `Array<GameInstance>`. The player is a thin subclass; the editor
embeds the same class. A game `: DefaultApplication` gets the defaults + its own; a game
`: IApplication` gets only what it declares.

## `ApplicationHost` + windows + graphics

`ApplicationHost` (`foundation.runtime.client`, `ApplicationHost.cppm`) is the concrete, generic host
that drives exactly one `IApplication`. It owns a `Context`, a borrowed optional `GraphicsDevice`, and
the LIST of `RenderWindow`s it presents. It is infrastructure - NOT subclassed - and LOOP-AGNOSTIC:
the shell layer drives `Start`/`Tick`/`Stop` (a blocking loop on desktop via `RunApplication` in
`foundation.runtime.desktop`; a callback on Emscripten via `foundation.runtime.web`), so the host holds
no run loop. It implements `IApplicationHost`, the view the application gets (`Ctx`, `Graphics`,
window open/close, exit).

Multi-window is uniform: the main window is `windows[0]`; every frame renders the whole list;
`OpenWindow`/`CloseWindow` run at runtime (the basis for detachable UI windows) with close deferred to
frame end. The GPU layer is three modules to keep the core host backend-agnostic and dodge a GCC
modules bug (importing a backend module into the core interface breaks GCC's reader):

- **`foundation.graphics`** - `GraphicsDevice` (shared: backend/adapter/device/queue + the
  frame-in-flight ring), `RenderWindow` (one per OS window: surface + swapchain + per-window present
  sync), `FrameContext` (the per-window per-frame host/consumer boundary).
- **`foundation.graphics.gpu`** - `CreateGraphicsDevice` (Vulkan/DX12).
- **`foundation.graphics.null`** - `CreateNullGraphicsDevice` (headless).

Headless stays first-class: no `GraphicsDevice` => no windows => render skipped (null-platform tests
untouched).

## Subsystems + per-window render

`Subsystem` (`foundation.runtime`) has the frame phases `BeginFrame` / `FixedUpdate` / `Update` /
`PostUpdate` / `EndFrame` plus lifecycle (`OnInit` / `OnReady` / `OnRegister` / `OnUnregister` /
`OnPrepareShutdown` / `OnShutdown`). There is NO `Subsystem::Render` phase - the original design
proposed one but it was never added; per-window rendering is driven by `IApplication::OnRenderWindow`
(given a `FrameContext` per window per frame), and rendering subsystems draw through that path.

## Embedded host (the editor)

The editor holds EXACTLY TWO contexts, forever: the editor-app context (chrome/tools/UIHost) and ONE
persistent embedded runtime `Context` owning all scene hosting (editing scenes, Simulate, preview
pages, and the Game tab's runs) plus every engine + game subsystem. `EmbeddedApplicationHost`
(`EmbeddedHost.cppm`) is the adapter: it routes `Ctx()` to the embedded runtime context (distinct from
the outer editor-app context) while SHARING the outer host's shell and the REAL `GraphicsDevice` (a
deliberate deviation from Sedulous's Graphics=null, which forced re-pointing subsystem device/window
after Configure). The embedded app renders into viewport textures, so `MainRenderWindow` is null and
window open/close are refused; "exit" stops the play session.

This is what lets a native game module `Configure()` ONCE and have its managers inject into the same
`SceneSubsystem` that hosts editing scenes - custom components are inspectable while editing and
playable in the Game tab, identical to standalone.

## Time + run ownership

Time is layered `host dt x context x group/instance x scene`, with the group/instance + fixed-step
accumulator owned by each `SceneManager` (see [[game-instance-track]] - the SceneManager model
superseded this doc's original per-scene-time framing). Run ownership - the script brain, the scenes,
per-instance net/input - moved up to `GameInstance`; `ApplicationHost` and the subsystems hold no run
state.

---

Design history (the original v1/v2 module-list model, the Sedulous wart table it corrected, the
GraphicsDevice/RenderWindow/FrameContext API sketches, and the v3 embedded-host phasing) is in
`Documentation/Archive/runtime-host-design-history.md`.
