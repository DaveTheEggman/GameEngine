# Runtime Host Hardening — Design (v2)

Status: **IMPLEMENTED (2026-06-26)** — both compilers green (clang + gcc, 34/34).

## Application model — INVERTED (2026-06-26, supersedes the module-list design below)

After reviewing how Sedulous actually structures apps (and why its
EngineApplication/EditorApplication both force their own default subsystems in,
so neither honors the game's real subsystem set), the module-list model below was
replaced with a single-application inversion:

- **`IApplication`** *is* the app/game (was `IApplicationModule`, now exactly ONE
  per host). It owns subsystem registration in `Configure(host)` — the host forces
  in nothing — so the subsystem set is the app's alone and identical standalone or
  embedded in the editor (**truly pluggable subsystems**). Hooks: `Settings()`,
  `Configure`, `OnStartup`, `OnLaunch`, `OnUpdate`, `OnFixedUpdate`,
  `OnRenderWindow`, `OnExit`, `OnShutdown`.
- **`ApplicationHost`** (was `Application`) is the generic, **final** host that
  drives one `IApplication`: owns Context, borrowed platform/graphics, the window
  list, the frame loop. No `On*` virtuals — it is infrastructure, not subclassed.
  Implements `IApplicationHost` (the app's view: `Ctx`/`Platform`/`Graphics`/
  `OpenWindow`/`CloseWindow`/`RequestExit`).
- **`DefaultApplication : IApplication`** lives in its OWN library
  (`raptor.runtime.defaultapp`, `Code/Raptor/Runtime/DefaultApp/`) so the base
  client never pulls in the engine subsystem libs — only apps that opt into the
  defaults link it. It's the registration point for the standard engine subsystems
  as they land. A game `: DefaultApplication` gets defaults + its own; a game
  `: IApplication` gets only what it declares.
- `RunApplication(IApplication&, IPlatform&, GraphicsDevice*)` (desktop) creates the
  `ApplicationHost` internally and drives it.

This collapses the module-list, its ordering/reverse-teardown question, and the
editor's subsystem duplication. Entry points (standalone `main`, editor) share the
one `IApplication`; the editor later runs the same app's `Configure` against its
embedded **runtime Context** (see the Context/Scene model note below).

**Deferred Context/Scene model (banked, not built):** the simulatable unit is a
**Scene** (lightweight, instantiable), NOT a Context. Heavy subsystems tick once in
a persistent runtime Context; the `SceneSubsystem` iterates active scenes.
Subsystems are `ISceneAware` and **inject component managers into scenes on
creation**; edit-vs-simulate is a per-scene `SimulationEnabled` flag gating
`simulationOnly` update functions **centrally** (no `if(playing)` in subsystems).
The editor = host Context (editor tools) + a second persistent **runtime Context**
populated by the game's `IApplication.Configure`. So the engine-subsystem bundle is
**context-scoped** (appliable to the host context standalone, or the editor's
runtime context), which `IApplication.Configure(host)` already is. Build when
scene/ECS exists.

## v3 — Embedded host + runtime context (SHIPPED 2026-07-18: H1 b65fec0, H2 b9cc5ce,
## model-resource move 028fff0, H2.5 f742d23, H3 bf69a19, H4 fda3634)

The banked Context/Scene model above is now the active plan, extended after (a) auditing
the shipped GameEditorPage against it (it diverged: the page reuses the EDITOR's context
subsystems, hand-ticks the game script, and swap/restores global input state per run) and
(b) a deep reference read of Sedulous's editor hosting (EditorApplicationHost +
EditorApplication.RuntimeContext - a real embedded host; the game IApplication runs
unchanged standalone or embedded). Decisions:

1. **Exactly TWO contexts, forever.** The editor-app context (editor chrome/tools/UIHost)
   and ONE persistent embedded **runtime Context** owning ALL scene hosting - editing
   scenes, Simulate, preview pages, and the Game tab's fresh runs - plus every engine +
   game subsystem. NOT context-per-page and NOT editing-vs-play contexts: a native game
   module Configure()s ONCE, and editing scenes can only inspect/serialize the module's
   custom components if its managers inject into the SAME SceneSubsystem that owns them
   (Sedulous's RuntimeContext works exactly this way).

2. **Isolation granularity = SCENE; service granularity = CONTEXT.** Time moves DOWN to
   the scene: `Scene::timeScale` + a per-scene FixedStepper accumulator + per-scene fixed
   alpha (physics interpolation reads the scene's alpha). SceneSubsystem feeds each scene
   dt * timeScale into its own accumulator. Pausing/slowing the Game tab cannot affect a
   Simulate page beside it - and two Simulate pages can run at different speeds, which
   even context-per-page could not do. Context::TimeScale remains as a rarely-used outer
   multiplier (effective = context x scene). Context-singleton services (input source,
   screen-tier UI, script services) serve "the running game" - unambiguous because the
   Game page is a SINGLETON BY DESIGN (documented invariant, matching Sedulous).

3. **DefaultApplication is the conventional application** (not forced - custom
   IApplications stay possible, design for them later). It registers ALL core subsystems
   in Configure() (scene/render/input/physics + future audio/UI/scripting) and OWNS the
   game-script lifecycle (script manager/context creation, Game class launch/update(dt)/
   exit, error-sink hookup, per-context service exposure) - moving that logic OUT of
   PlayerApplication's Main and OUT of GameEditorPage. The player becomes a thin
   DefaultApplication consumer; the editor embeds the SAME DefaultApplication.

4. **EmbeddedApplicationHost : IApplicationHost** - a thin adapter owned by the editor
   app: routes Ctx to the runtime context; SHARES the shell and the REAL GraphicsDevice
   (deviation from Sedulous's Graphics=null, which forced it to re-point subsystem
   device/window after Configure - we avoid that wart); MainWindow = null; window
   open/close = no-ops; RequestExit = stop the play session (deferred to frame end).
   Shared infrastructure (device, fonts, shader system, resource manager) is pre-injected
   with an owns-flag so app teardown skips borrowed pieces (Sedulous PresetInfrastructure
   pattern).

5. **Ticking is layered.** The editor app ticks the runtime context's standard lanes
   (BeginFrame -> fixed loop -> Update -> PostUpdate -> EndFrame) every frame; the Game
   page only drives the IApplication PLAY BRACKET: Play = OnLaunch(embeddedHost), per
   frame OnUpdate while running, Stop = OnExit. The page keeps: viewport + toolbar +
   bracket + its InputSurface, which becomes the runtime InputSubsystem's PERMANENT
   source (no more per-run SetSourceProvider/SetMap swap-and-restore). Editor code never
   reaches into game internals.

6. **Simulate (8a) is untouched** - in-place snapshot/run/restore on the (migrated)
   scenes stays a deliberately lighter mechanism than the Game tab, as in Sedulous.

Known costs accepted: migrating editing scenes off the editor context's SceneSubsystem
(the biggest step - hierarchy/inspector/gizmos/commands re-point to the runtime context's
scenes); physics subsystem reads per-scene alpha instead of Context::FixedAlpha; the
Wren Physics/Input service bindings become per-run-context naturally (this DELETES the
latent "first live world" ambiguity between Simulate and the Game tab).

Native-module story this enables: editor loads the module DLL -> its IApplication (a
DefaultApplication subclass by convention) Configure()s against the embedded host ->
managers appear in every scene -> custom components are inspectable while editing and
playable in the Game tab, identical to standalone.

### v3 phasing

- **H1 - per-scene time**: Scene::timeScale + per-scene FixedStepper + per-scene alpha;
  physics/input/script consumers re-pointed; tests (two scenes at different scales; pause
  isolation).
- **H2 - DefaultApplication consolidation**: core-subsystem registration + game-script
  lifecycle move from Player Main + GamePage into DefaultApplication; player slims to a
  thin subclass; behavior identical (player smoke + editor smoke).
- **H3 - embedded host**: EmbeddedApplicationHost + persistent runtime context in the
  editor; DefaultApplication configured against it; scene-hosting migration (editing/
  Simulate/preview scenes move to the runtime context).
- **H4 - GamePage shrink**: page = viewport + toolbar + play bracket + InputSurface-as-
  permanent-source; delete the hand-rolled script ticking + input swap/restore.
- THEN game-UI P1 builds on the embedded shape (its overlay pass + consumption mask land
  in DefaultApplication/UISubsystem once, serving player + Game tab identically).

## Implementation status & deviations from plan

All four phases landed (multi-window platform, render host, application host + the
inverted single-`IApplication` model, smoke sample). Deviations worth noting:

- **Graphics module split into three** (not the planned two files) to keep the core
  host GPU-backend-agnostic *and* dodge a GCC modules bug: importing a backend
  module (`raptor.rhi.null`/`vk`) into the core *interface* makes GCC's module
  reader fail ("Bad file data / failed to load pendings"). So:
  - `raptor.runtime.graphics` (core) — `GraphicsDevice` (+ `FromBackend`),
    `RenderWindow`, `FrameContext`; imports only base `raptor.rhi` + platform.
  - `raptor.runtime.graphics.null` — `CreateNullGraphicsDevice` (headless).
  - `raptor.runtime.graphics.gpu` — `CreateGraphicsDevice` (Vulkan/DX12).
- **Frame ring split**: `GraphicsDevice` owns the ring *index*
  (`CurrentFrame`/`AdvanceFrame`); each `RenderWindow` owns its own command-pool +
  fence ring (per-window present sync, decision #2). `RenderWindow::BeginFrame()`/
  `EndFrame()` read the device's current frame (no `GraphicsDevice&` param).
- **`Subsystem::Render(FrameContext&)` deferred.** Base `Subsystem`/`Context` stay
  graphics-free (no RHI dependency on the pure-logic runtime lib). Per-window
  render is driven by `Application::OnRenderWindow` (virtual) +
  `IApplicationModule::OnRenderWindow`. The subsystem-level render phase lands with
  the first real rendering subsystem (UI/renderer port), via a graphics-aware
  subsystem base, not plain `Subsystem`.
- **Runner kept in the desktop platform module** (`RunApplication`, now taking a
  `GraphicsDevice*`) rather than a new `raptor.runtime.runner` module — less churn;
  the runner is execution-model-specific and already lived there.
- The proof sample is **`Code/Samples/MultiWindow`** (two windows, different clear
  colors) on the client stack; RHI samples were left untouched.

---

Original design follows.

Status (original): **proposed (for review, no code yet)**
Supersedes v1. Revised after studying Sedulous `origin/master` (app/module/
subsystem arch + multi-window/docking) and feedback:
- RHI samples stay on their own host — they exercise RHI, not the client stack.
- **Multi-window is first-class** (editor + uisandbox detach panels into real OS
  windows).
- **No EngineApplication-vs-client split**: one `Application` + composable
  `IApplicationModule`s, with a default module registering the standard subsystems.

Goal: one tested host — shared GPU device + N windows + frame loop + module
extension — that samples, the future UI (multi-root docking), and the renderer all
attach to through one contract.

---

## 1. What we're correcting from Sedulous (design with hindsight)

Sedulous works but "fell into place." A fresh design fixes:

| Sedulous wart (origin/master) | Raptor v2 |
|---|---|
| `EngineApplication` and client `Application` are **separate hierarchies**; the only real delta is default-subsystem registration | **One concrete `Application`**; "engine app" = Application + a `DefaultSubsystemsModule`. No second hierarchy. |
| Hosts **one** `IApplicationModule` | `Application` holds a **composable list** of modules (defaults + game + tools). |
| **Main window special-cased** vs secondary windows (duplicate acquire/render/present) | **Uniform window list**; the main window is just `windows[0]`. One render path. |
| Per-window render data is an untyped `Object` (`SecondaryWindowContext.UserData`) | Typed per-window **`RenderWindow`** + a typed user-data slot (interface, not `Object`). |
| `IDockableWindowHost` (create OS window on detach) **reimplemented in every app** (Editor, UISandbox) | The **framework provides** the dockable-window host over the window manager; apps don't reimplement it. |
| Single shared frame fence across all windows | Per-frame fence ring on the shared device; **per-window present sync** (cleaner; §4.3 decision). |
| Window destroy needs ad-hoc `mPendingDestroys` tracking | Deferred-destroy is a first-class part of the window manager. |

What Sedulous gets right and we keep: single shell/event-pump for all windows;
shared device, per-window swapchain; one UI context with N root views + shared
input managers (cross-window drag); `OnLaunch`/`OnExit` play-mode pair so a module
runs standalone *or* embedded in the editor.

## 2. Current Raptor building blocks

| Piece | File | Status for v2 |
|---|---|---|
| `Application` (lifecycle, fixed-step loop, `On*` hooks) | `Runtime/Client/Application.cppm` | ✅ base; gains modules + window list + render wiring |
| `Subsystem` (lifecycle + 5 frame phases) | `Runtime/Subsystem.cppm` | ✅ gains a `Render(FrameContext&)` phase |
| `Context` (owns/drives subsystems) | `Runtime/Context.cppm` | ✅ |
| `IPlatform` / `IWindow` | `Runtime/Platform/Platform.cppm` | ⚠️ **single `MainWindow()` → needs a window manager (N windows + per-window events)** |
| `SDL3Platform` / `NullPlatform` | `Runtime/Platform/{Desktop,Null}` | ⚠️ extend to multi-window |
| RHI: `SwapChain`/`Surface`/`Queue`/`Fence`/`CommandPool` | `RHI/*` | ✅ sufficient |
| RHI host bring-up | only in `SampleApp` | ⚠️ promote into the runtime (shared device + per-window) |

## 3. Principles

1. **Loop-agnostic Application** stays loop-agnostic (`Start`/`Tick`/`Stop`);
   desktop/Emscripten runners drive it. `RunDesktop` is a convenience wrapper.
2. **Headless stays first-class**: no `GraphicsDevice` ⇒ no windows ⇒ render
   skipped; null-platform tests untouched.
3. **Multi-window native, single-window trivial**: the host always owns a *list*
   of windows; a one-window app is just `count == 1`. Multi-window is not a
   separate code path.
4. **Shared GPU, per-window presentation**: one `GraphicsDevice` (backend/adapter/
   device/queue + frame ring); each window owns only its surface/swapchain.
5. **Two-level frame API**: host owns plumbing (acquire/sync/submit/present/resize)
   per window; consumer owns content. A `BeginBackbufferPass` convenience covers
   2D/UI; a RenderGraph uses the raw encoder.
6. **Composition over inheritance for app setup**: behavior comes from modules +
   subsystems, not from an application subclass tower.
7. Raptor conventions: named modules, `-fno-exceptions`/`-fno-rtti`, own containers,
   `Status`/`Result`, UTF-8, doctest headless tests, both compilers.

## 4. Architecture

### 4.1 `GraphicsDevice` — the shared GPU host (new)

Module `raptor.runtime.graphics`. Created **once**. Owns backend (validation-
wrapped) → adapter → device → graphics queue, plus the **frame-in-flight ring**
(N command pools + N fences, N = `framesInFlight`). Backend-agnostic; the
Vulkan/DX12 + validation choice lives here only.

```cpp
struct GraphicsDeviceDesc {
    BackendType    backend          = BackendType::Vulkan;
    bool           enableValidation = true;
    u32            framesInFlight   = 2;
    DeviceFeatures requiredFeatures = {};
};

class GraphicsDevice {
public:
    static Result<UniquePtr<GraphicsDevice>> Create(const GraphicsDeviceDesc&);
    ~GraphicsDevice();                 // WaitIdle + ordered teardown

    // Build a presentation target for a window (surface + swapchain).
    Result<UniquePtr<RenderWindow>> CreateRenderWindow(IWindow&, const RenderWindowDesc&);

    Device* Raw()   noexcept;
    Queue*  Queue() noexcept;
    u32     FramesInFlight() const noexcept;
    u32     CurrentFrame()   const noexcept;   // 0..N-1 ring index

    // Advance the CPU frame ring once per app frame (waits the next fence,
    // resets that frame's pool). Called by Application::Tick before per-window render.
    void    BeginFrame();
    void    EndFrame();                        // advance ring index
};
```

### 4.2 `RenderWindow` — a window's presentation target (new)

Sedulous's `SecondaryWindowContext`, generalized and typed. One per OS window
(including the main one). Created/destroyed at runtime (detach/redock).

```cpp
struct RenderWindowDesc {
    TextureFormat format      = TextureFormat::RGBA8UnormSrgb;
    PresentMode   presentMode = PresentMode::Fifo;
    u32           bufferCount = 2;
};

class RenderWindow {
public:
    IWindow&  Window() noexcept;
    SwapChain* Swap()  noexcept;

    // Poll the window size; resize the swapchain if it changed (returns true).
    bool       SyncSize();

    // Per-frame: acquire this window's backbuffer + open a host-created encoder
    // from the device's current-frame pool. Invalid if minimized/zero-sized.
    FrameContext BeginFrame(GraphicsDevice&);
    // Finish encoder → transition → submit (per-window fence value) → present.
    void         EndFrame(GraphicsDevice&, FrameContext&);

    // Typed per-window payload (UI root, VG renderer, viewport...). NOT Object.
    IRenderWindowData* Data() noexcept;
    void               SetData(UniquePtr<IRenderWindowData>);
};
```

`IRenderWindowData` is a tiny capability interface (own `As*()` idiom) so the UI
layer can stash a per-window `{RootView, VGContext, VGRenderer}` without the host
knowing the type.

### 4.3 `FrameContext` — host/consumer boundary (per window, per frame)

```cpp
struct FrameContext {
    bool            valid       = false;   // minimized => skip
    RenderWindow*   window      = nullptr; // which window this frame targets
    u32             frameIndex  = 0;       // device ring index (0..N-1)
    u32             width = 0, height = 0;
    CommandEncoder* encoder     = nullptr; // primary (host-created)
    CommandPool*    pool        = nullptr; // for extra encoders
    Texture*        backbuffer  = nullptr;
    TextureView*    backbufferView = nullptr;

    RenderPassEncoder* BeginBackbufferPass(ClearColor); // 2D/UI convenience
    void               EndBackbufferPass();
};
```

### 4.4 Platform: single-window → window manager

`IPlatform` today exposes one `MainWindow()`. v2 generalizes it (mirrors Sedulous
`IShell` → `IWindowManager`, but keeping our passive-service stance):

```cpp
class IWindowManager {
public:
    Result<IWindow*> CreateWindow(const WindowSettings&); // runtime create
    void             DestroyWindow(IWindow*);             // deferred to frame end
    Span<IWindow* const> Windows() const noexcept;        // all open windows
    IWindow*         MainWindow() noexcept;               // == Windows()[0]
    // Per-window OS events (resize/move/focus/close) delivered here each pump.
    Delegate<void(const WindowEvent&)> onWindowEvent;
};

class IPlatform {            // unchanged otherwise
    virtual IWindowManager* Windows() noexcept = 0;
    virtual IInputManager*  Input()   noexcept = 0;
    virtual void            ProcessEvents() = 0;          // pumps ALL windows
    virtual bool            IsRunning() const noexcept = 0;
    virtual void            RequestExit() = 0;
};
```

`NullPlatform` returns a window manager with zero (or one fake) window for
headless tests. SDL3 maps `SDL_PollEvent` window events onto `onWindowEvent`.

### 4.5 `Application` — one concrete host, modules + windows

No `EngineApplication`. `Application` is concrete: it owns the `Context`, the
borrowed `IPlatform`, an optional `GraphicsDevice`, the **list of `RenderWindow`s**,
and a **list of `IApplicationModule`s**. Per-frame it renders **every** window
uniformly.

```cpp
class Application : public IApplicationHost {
public:
    void AddModule(UniquePtr<IApplicationModule>);  // defaults + game + tools
    void Start(IPlatform* = nullptr, GraphicsDevice* = nullptr);
    void Tick(f32 dt);
    void Stop();

    // Runtime window mgmt (also the basis of the framework dockable host).
    RenderWindow* OpenWindow(const WindowSettings&, const RenderWindowDesc&);
    void          CloseWindow(RenderWindow*);        // deferred destroy

    // IApplicationHost (what modules see): Ctx(), Graphics(), Platform(),
    // Input(), OpenWindow(), RequestExit(), asset dirs, RuntimeScene...
};
```

`Tick(dt)` (render section, multi-window):

```cpp
m_context.BeginFrame(dt); FixedUpdate*; m_context.Update(dt);
for (auto* m : m_modules) m->OnUpdate(*this, dt);
m_context.PostUpdate(dt);

if (m_graphics) {
    m_graphics->BeginFrame();                       // wait/reset this frame's ring slot
    for (RenderWindow* w : m_windows) {             // main is just w[0]
        w->SyncSize();
        FrameContext f = w->BeginFrame(*m_graphics);
        if (!f.valid) continue;
        m_context.Render(f);                        // subsystems draw this window
        for (auto* m : m_modules) m->OnRenderWindow(*this, f);
        w->EndFrame(*m_graphics, f);
    }
    m_graphics->EndFrame();                         // advance ring
}
m_context.EndFrame();
m_windowMgr->FlushPendingDestroys();
```

### 4.6 `IApplicationModule` — the extension point

```cpp
class IApplicationModule {
public:
    virtual void  Configure(IApplicationHost&) {}        // register subsystems/types
    virtual void  OnStartup(IApplicationHost&) {}         // after Context.Startup
    virtual void  OnLaunch(IApplicationHost&) {}          // enter play (standalone: once; editor: Play)
    virtual void  OnUpdate(IApplicationHost&, f32) {}
    virtual void  OnFixedUpdate(IApplicationHost&, f32) {}
    virtual void  OnRenderWindow(IApplicationHost&, FrameContext&) {} // optional per-window draw
    virtual void  OnExit(IApplicationHost&) {}            // leave play
    virtual void  OnShutdown(IApplicationHost&) {}        // before Context.Shutdown
};
```

- **`DefaultSubsystemsModule`** registers the standard subsystems once we have them
  (input/scene/render/etc.). Adding it = Sedulous's "EngineApplication" with no new
  class. A bare `Application` with no modules = a tool host (ModelViewer-style).
- Modules are **composable & ordered**: defaults first, then game/tool modules add
  or patch in `Configure` (subsystem `UpdateOrder` still resolves run order).
- Entry point shrinks to: create platform + device, `app.AddModule(defaults)`,
  `app.AddModule(game)`, `RunDesktop(app, platform)`.

### 4.7 `Subsystem::Render(FrameContext&)`

Add a `Render(FrameContext&)` phase (default no-op) so subsystems draw per window
in `UpdateOrder`. The future `UISubsystem`/`RenderSubsystem` record here; 2D-after-
3D is just ordering. Called once **per window** per frame (subsystem inspects
`f.window`/`f.window->Data()` to pick what to draw there).

### 4.8 `DesktopRunner`

Thin loop owner (own module `raptor.runtime.runner`): `Start` → while
`platform.IsRunning() && app.IsRunning()` { `ProcessEvents`; dt; `Tick` } → `Stop`;
return `ExitCode`. No RHI. Emscripten gets a sibling later.

## 5. Multi-window frame flow

```
Runner: ProcessEvents()  ── pumps ALL windows ; onWindowEvent(resize/close) per window
App.Tick(dt):
  Context update phases + module OnUpdate
  GraphicsDevice.BeginFrame()                 // ring slot: wait fence, reset pool
  for each RenderWindow w (main == windows[0]):
      w.SyncSize()                            // resize swapchain if window changed
      f = w.BeginFrame(device)               // acquire + encoder (+ bb→RT)
      if !f.valid: continue                   // minimized
      Context.Render(f)                       // subsystems draw THIS window
      modules.OnRenderWindow(f)
      w.EndFrame(device, f)                   // bb→Present, submit(per-window fence), present
  GraphicsDevice.EndFrame()                   // advance ring index
  windowMgr.FlushPendingDestroys()            // safe teardown of closed windows
```

## 6. Forward path — how UI docking attaches (not built now, but the host must fit it)

When the UI lands, multi-window docking falls out of this host with **no per-app
boilerplate**:

- One UI context, **N `RootView`s** (one per `RenderWindow`), shared input/focus/
  dragdrop (cross-window drag) — the Sedulous model we keep.
- The **framework** implements `IDockableWindowHost` over `Application::OpenWindow/
  CloseWindow` + the window manager — apps don't reimplement it (fixing the
  Editor/UISandbox duplication).
- Detach: `DockManager.FloatPanel` → framework host → `OpenWindow` →
  `RenderWindow` whose `IRenderWindowData` holds `{RootView, VGContext, VGRenderer}`
  → reparent the panel subtree into the new RootView. Redock reverses it;
  `CloseWindow` defers GPU/window teardown to frame end.
- The per-window draw is just a `UISubsystem::Render(f)` that runs `f.window`'s
  RootView through VG into `f.BeginBackbufferPass()`.

## 7. Module / file layout

```
Code/Raptor/Runtime/
  Graphics/
    GraphicsDevice.cppm    // raptor.runtime.graphics (GraphicsDevice, BackendType, descs)
    RenderWindow.cppm      //   :window  (RenderWindow, IRenderWindowData, FrameContext)
    Tests/...              // headless: device create, window create/destroy, frame ring, resize
  Runner/DesktopRunner.cppm
  Platform/Platform.cppm   // + IWindowManager, WindowEvent; IPlatform.Windows()
  Platform/Desktop/SDL3Platform.cppm  // multi-window + per-window events
  Platform/Null/NullPlatform.cppm     // headless window mgr
  Client/Application.cppm  // concrete; modules list + window list + render loop + IApplicationHost
  Client/ApplicationModule.cppm  // IApplicationModule, IApplicationHost, DefaultSubsystemsModule (stub)
  Subsystem.cppm           // + Render(FrameContext&)
```

`SampleApp` (Code/Samples/Framework) is **left untouched** — RHI samples keep their
own host. (Optionally it can later be reimplemented over `GraphicsDevice` to reduce
dup, but RHI samples do NOT move to the client stack.)

## 8. Testing (headless, both compilers, NullPlatform + Null RHI)

- `GraphicsDevice::Create` + teardown; frame ring advances `0..N-1`.
- Window manager: create/destroy windows; `Windows()` reflects it; main == [0];
  deferred destroy flushes at frame end.
- `RenderWindow::BeginFrame/EndFrame` over null RHI; minimized ⇒ invalid frame.
- Multi-window: 2 null windows render in one `Tick`; both presented; close one →
  next frame renders only the survivor.
- `Application` + modules: `AddModule`; `Configure/OnStartup/OnLaunch/OnUpdate/
  OnExit/OnShutdown` fire in order; `DefaultSubsystemsModule` registers subsystems;
  headless (no device) path unchanged.
- Resize: null window size change ⇒ swapchain `Resize` + `OnResize`/event once.

## 9. Decisions — LOCKED (2026-06-26)

1. **Multi-window: build the whole thing now.** Window-manager + `RenderWindow`
   list + runtime create/destroy in this pass (single-window = N=1). UI docking UX
   (§6) still rides on top during the later UI port, but the host is fully
   multi-window now.
2. **Per-window present sync** within the shared frame ring (not Sedulous's single
   shared fence).
3. **Backbuffer transitions host-managed by default**; raw `encoder` exposed for a
   RenderGraph that owns its own barriers.
4. **Composable list** of `IApplicationModule` (defaults + game + tools).
5. **Both mechanisms:** keep `Application`'s `On*` virtuals (tools/samples subclass
   directly — e.g. the multi-window smoke sample) **and** support modules (game
   logic lives in modules). `Application` stays subclassable, not final. The
   per-window render hook exists in both forms: virtual `Application::OnRenderWindow
   (FrameContext&)` and `IApplicationModule::OnRenderWindow`.
6. **API change accepted:** the no-arg `OnRender()` becomes per-window
   `Subsystem::Render(FrameContext&)` + `Application::OnRenderWindow(FrameContext&)`.
   No external consumers.

## 10. Rollout

1. Platform: add `IWindowManager` + per-window events; multi-window `SDL3Platform`;
   `NullPlatform` window mgr. Tests.
2. `GraphicsDevice` + `RenderWindow` + `FrameContext` + headless tests.
3. `Application` v2: modules list, window list, multi-window render loop,
   `IApplicationHost`; `Subsystem::Render`; `DefaultSubsystemsModule` stub. Tests.
4. `DesktopRunner`; a tiny **multi-window smoke sample** on the client stack (two
   windows, clear colors) as the proof — NOT an RHI sample, a runtime-host sample.
5. (UI port, later) framework `IDockableWindowHost` over `OpenWindow`; per-window
   UI root data. Detach/redock falls out of §6.
```
