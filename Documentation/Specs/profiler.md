# Profiler - CPU and GPU on one timeline, in the editor and in a capture file

> STATUS: SPEC 2026-10-13, decisions taken (below); not started. Backlog entry it closes:
> `Plans/backlog-0.1.md` (Seeded: GPU profiler + profiler visualization). Read CONVENTIONS.md first.

## What it is for

The user asked (2026-08-26) for two things: GPU timings past what the render graph prints today,
and a visual profiler that puts the CPU's scopes and the GPU's passes on ONE per-frame timeline
instead of two text dumps. It is a 0.1 item: a user making a game needs to see where a frame goes,
and agents driving the engine over MCP need the same numbers.

## Where things stand (read in the code, 2026-10-13)

The backlog item is out of date in three places: the CPU profiler is `foundation.profiler`
(not `draconic.profiler`), GPU timestamps work on DX12 and native WebGPU as well as Vulkan, and
the `profiler-system` and `imgui-integration` docs it links do not exist.

**CPU: `foundation.profiler`** (`Code/Foundation/Profiler/Profiler.cppm`, macros in `Profiler.h`).
- `PROFILE_SCOPE(name)` (RAII), `PROFILE_FRAME_BEGIN/END`, compiled in where `BUILD_PROFILING`
  is 1: Debug and RelWithDebInfo (`CMakeLists.txt:220`); out in Release and Shipping. A runtime
  `SetEnabled`, on by default.
- Each thread appends `ProfileSample{name, startTick, durationTicks, depth, threadIndex}` to its
  own list (thread_local, registered under a mutex); `EndFrame` drains every thread into ONE
  completed frame, overwritten each frame. No history beyond a 64-entry ring of frame times, no
  thread names, nothing in the job system.
- Frames are bracketed in `ApplicationHost::Tick` (Update, Render, Acquire, Present, Advance);
  about 40 scopes across render, animation, audio, navigation, particles, physics, script,
  terrain, UI and vegetation.
- Read out only as text: the P key (`DefaultApplicationImpl.cpp:130`) prints `BuildReport()`.

**GPU: `GraphProfiler`** (`Code/Foundation/RenderGraph/GraphProfiler.cppm`).
- One timestamp query set (64 passes x 2) and one readback buffer, no ring per frame in flight;
  a timestamp before each executed pass's barriers and after its last transition.
- Turned on unconditionally (`RenderSubsystemImpl.cpp:977`), in every build, Shipping included.
- Read only on demand, by `WaitIdle()` and a read of the last frame: no frame tag, no history,
  no capability check (`Features.timestampQueries` is never asked).
- The graph also times each pass's CPU recording (`m_passCpu`), printed beside it.

**The RHI** has what a profiler needs: `QueryType::Timestamp` / `PipelineStatistics`,
`WriteTimestamp` on encoders, `ResolveQuerySet`, `Queue::TimestampPeriod()`, and render-pass
boundary timestamps in `RenderPassDesc`. Vulkan: full. DX12: full. WebGPU native: encoder
timestamps when wgpu-native grants `TimestampQueryInsideEncoders`, pass-boundary timestamps
always; in a browser, none (a no-op). Null: stubs. Pipeline statistics pools exist on Vulkan and
DX12; nothing reads them.

**UI**: nothing draws a timeline. The editor is built on `foundation.ui` docking (no ImGui);
`LogView` is the model of a panel fed every frame. ImGui (`extensions.imgui`) is in the samples
only. No MCP tool returns timings.

**Tests**: `Profiler.Tests` (4 cases), one GraphProfiler case (uninitialized is safe). Nothing
tests a timestamp on any backend.

## What other engines do (surveyed 2026-10-13, current code)

| | CPU record | GPU timing | CPU/GPU clock | Viewer | Gate |
|---|---|---|---|---|---|
| **Lumix** | begin/end events into 4 KB pages per thread, a ring of 500 pages (2 MB); fibers, jobs, mutex waits, counters, context switches (Windows) | timestamp per begin/end, nested; 2 frames late; NVML clock and memory counters | once at start, an offset, no drift fix | in-editor ImGui timeline: a lane per thread, a GPU lane, counters, CPU block linked to its GPU block; frozen snapshots; own binary save | always on |
| **ezEngine** | one record per scope at its end, a ring per thread (4 MB main, 1 MB others); scopes under 0.1 ms dropped | nested scopes, frame-tagged pools, read when the frame's fence passed, kept 4 frames | once (a timeline semaphore and a sample), no drift fix | no viewer: writes Chrome trace JSON (frames lane, GPU lanes, thread lanes) for chrome://tracing or Perfetto; Tracy optional | off in Shipping |
| **Traktor** | begin/end events, names interned, flushed 64 at a time to a live listener | per render-graph pass, flat; read when the swapchain image comes back | none: GPU bars placed at the CPU time of the graph build | editor dialog over the editor-to-game connection: perf counters and a lane chart; live only, no files | listener present |
| **LunarSong** | aggregates per name (a call tree for the main thread), 600 frames of history | per render-graph pass, flat; 3 frames late; Vulkan, Metal, WebGPU | none | editor panels: a CPU tree with a frame-time chart and scrubbing, a CPU and GPU per-pass table, counter charts; no timeline; extensive MCP tools (`get_cpu_profiler`, `get_gpu_profiler`, ...) | compiled in, armed by a panel |
| **ours** | per-thread samples, one frame kept | per pass, flat, read by stalling | none | P key text | Debug/RelWithDebInfo (CPU); GPU always |

None of them uses calibrated timestamps (`VK_KHR_calibrated_timestamps`, D3D12
`GetClockCalibration`); the two that align clocks do it once, with no drift correction. Only
Lumix has a real timeline viewer in its editor; ez leans on Chrome/Perfetto, which costs a file
writer and gives a full viewer for free.

## Design

### One capture layer, UI-free

`foundation.profiler` becomes the one store both halves write to, and every viewer reads:

- **Per thread**: a fixed ring of begin/end events (name id, tick), sized per thread (main 64K
  events, others 16K; a setting), written without a lock by its own thread. Names are static
  literals or interned once (a dynamic name, a pass's, interns to an id). Threads carry names
  (`SetThreadName`), and the job system names its workers and scopes each job it runs.
- **Frames**: `FrameBegin` records the frame number and tick on the main thread; the store keeps
  a history of frames (300 by default, about five seconds) as index ranges into the rings, so a
  viewer can select any recent frame without copying.
- **GPU lane**: GPU passes arrive late (below) and are written into the frame they belong to,
  by frame number, as begin/end ticks already on the CPU clock.
- **Counters**: named values per frame (draw calls, triangles, GPU memory where the backend
  says), the lane below the timeline.
- **Armed or not**: recording costs a branch when nothing is watching. A viewer (the panel, a
  capture, an MCP call) arms it; the P key keeps working.

### GPU timings: frame-tagged, read without stalling, on the CPU clock

`GraphProfiler` keeps one query set per frame in flight, tagged with the CPU frame number, and
reads a frame's results when that frame's fence has passed (the slot comes round), never by
`WaitIdle`. Each executed pass writes begin/end; render-graph markers inside a pass (a named
scope) may nest. It asks `Features.timestampQueries` and turns itself off where there are none
(a browser), so the GPU lane is simply absent there.

Aligning the clocks: where the device offers calibrated timestamps (Vulkan
`VK_KHR/EXT_calibrated_timestamps`, D3D12 `GetClockCalibration`), sample a CPU/GPU pair every
second or so and map each GPU tick through the latest pair, which also corrects drift; elsewhere
(WebGPU native), a sync point once at arm time (a timestamp written and waited for, the CPU tick
taken then), as ez does. A test checks a frame's GPU passes land inside or after their CPU frame
and in order.

The GPU profiler is compiled under the same gate as the CPU one and armed with it, instead of
running in every build.

### Viewers

- **The editor's Profiler panel** (in the View menu, beside Console), on `foundation.ui`:
  - a frame strip: one bar per frame in the history, its height the frame's time, a budget line
    (16.7 / 33.3 ms); click selects a frame, Pause freezes the history;
  - the timeline of the selected frame (or a dragged range of frames): a lane per thread, nested
    scopes as stacked bars, the GPU lane with its passes, frame boundaries; wheel zooms about the
    pointer, drag pans, hover shows name, duration and depth;
  - a table beside it: scopes and passes of the selection by total time, self time and count,
    sortable; selecting a row highlights its bars.
  It shows the editor's own frames and a Play-in-editor game's (the Game tab runs in-process).
- **A capture file in Chrome trace JSON**: Save Capture in the panel, a key and a command-line
  flag in any player (`--profile-capture <seconds>`), and the MCP tool. It opens in Perfetto or
  chrome://tracing with a frames lane, a lane per thread and the GPU lane, so an exported game on
  a Steam Deck or a phone is profiled without the editor.
- **MCP**: `profiler_capture` arms the profiler for N frames (of the editor or a PIE instance),
  writes the trace file and answers a summary: frame times (min, median, max), the top scopes and
  GPU passes by total time, and the path. Documented in the McpGuide; the tool count bumps.

## Decisions (user 2026-10-13)

1. **Viewer**: the editor's Profiler panel plus Chrome trace capture files. No in-game overlay.
2. **Profiling an exported game**: capture files for 0.1. A live connection from the editor to a
   running player is in `Plans/backlog-0.2.md`.
3. **Gate**: compiled into Debug, RelWithDebInfo and Release; out only in Shipping
   (`BUILD_PROFILING`, `CMakeLists.txt:220`), the GPU profiler with it.
4. **Pipeline statistics and GPU memory counters**: after 0.1.

## Phases

- **P0 capture store**: per-thread event rings, thread names, frame history, the armed switch,
  job-system scopes; the P key reads the new store. Tests: nesting across threads, the ring
  wrapping, a frame selected from history, nothing recorded unarmed.
- **P1 GPU**: frame-tagged query sets per frame in flight, read without stalling, capability
  check, clock alignment per backend, under the gate. GPU probe tests on Vulkan and WebGPU
  native: passes arrive tagged with their frame, non-overlapping, ordered, inside the frame's
  CPU span after alignment.
- **P2 capture file and MCP**: the Chrome trace writer, the player key and flag,
  `profiler_capture`. Tests: the JSON round-trips (frames, threads, GPU lane, nesting), the tool
  answers its summary.
- **P3 the Profiler panel**: frame strip, timeline, table, pause, zoom; docs page in the user
  docs. The user checks it by eye.
- **Later** (after 0.1): pipeline statistics and GPU memory counters; a live connection to a
  running player (backlog-0.2).
