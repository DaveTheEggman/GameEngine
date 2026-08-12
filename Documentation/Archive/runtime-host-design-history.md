# Runtime Host - design history (archived)

> Status: ARCHIVED
> Superseded by: Documentation/Systems/runtime-host.md
> Track: [[runtime-host-v3]]

NON-AUTHORITATIVE. The design evolution of the runtime host (v1 -> v2 inverted -> v3 embedded).
Present-tense truth is `Systems/runtime-host.md`; the full original design doc (all API sketches, the
multi-window frame flow, the v3 phasing H1-H4) is in git at the P0 commit 3b92560d. Kept for the "why".

## The evolution

- **v1** proposed one `Application` + a composable list of `IApplicationModule`s, with a default module
  registering the standard subsystems.
- **v2 (the inversion, 2026-06-26)** replaced the module list after seeing that Sedulous's
  EngineApplication/EditorApplication both force their own default subsystems in (so neither honors the
  game's real subsystem set). `IApplication` BECAME the app (exactly one per host, owning subsystem
  registration in `Configure`), `ApplicationHost` became the generic final host driving it, and
  `DefaultApplication` moved to its own library so the base client never pulls in the engine subsystem
  libs. This collapsed the module-list + its ordering/teardown question + the editor's subsystem
  duplication. `IApplicationModule` is gone.
- **v3 (embedded host, 2026-07-18)** added the persistent embedded runtime context so the editor hosts
  the SAME `IApplication` unchanged (Sedulous EditorApplicationHost lineage), with per-scene time and
  the game-script lifecycle consolidated into `DefaultApplication`. Later, run ownership moved up again
  into `GameInstance` + `SceneManager` (see the game-instance design history) - the current model.

## What v2 corrected from Sedulous

| Sedulous wart | Raptor |
|---|---|
| `EngineApplication` and client `Application` are separate hierarchies (only real delta: default-subsystem registration) | ONE `IApplication`; "engine app" = `DefaultApplication`. No second hierarchy. |
| Hosts ONE application module | The inverted model: the app IS the `IApplication`; no module list. |
| Main window special-cased vs secondary windows | Uniform window list; main is `windows[0]`; one render path. |
| Per-window render data an untyped `Object` | Typed `RenderWindow` + `IRenderWindowData` (capability interface). |
| `IDockableWindowHost` reimplemented in every app | The framework provides the dockable host over `OpenWindow`/`CloseWindow`. |
| Single shared frame fence across all windows | Per-window present sync within the shared frame ring. |
| Ad-hoc `mPendingDestroys` | Deferred-destroy is first-class in the window manager. |

Kept from Sedulous: single shell/event pump for all windows; shared device + per-window swapchain; one
UI context with N root views + shared input (cross-window drag); the `OnLaunch`/`OnExit` play pair so
an app runs standalone or embedded.

## Deviations + calls that shipped

- **GraphicsDevice split into three modules** (`foundation.graphics` core + `.gpu` + `.null`) to keep
  the core host backend-agnostic AND dodge a GCC modules bug (importing a backend module into the core
  interface makes GCC's reader fail).
- **Frame ring split**: `GraphicsDevice` owns the ring index (`CurrentFrame`/`AdvanceFrame`); each
  `RenderWindow` owns its own command-pool + fence ring (per-window present sync).
- **`Subsystem::Render(FrameContext&)` deferred, then obsoleted.** The base `Subsystem`/`Context` stay
  graphics-free; per-window render was to land as a subsystem phase with the first rendering subsystem.
  In the shipped code it never became a `Subsystem` phase - per-window render is driven by
  `IApplication::OnRenderWindow` instead. So this design item resolved DIFFERENTLY, not as a pending
  backlog.
- **Runner kept in the desktop platform module** (`RunApplication`) rather than a separate runner
  module.
- The v3 embedded host SHARES the real `GraphicsDevice` (deviation from Sedulous's Graphics=null, which
  forced re-pointing subsystem device/window after Configure).
