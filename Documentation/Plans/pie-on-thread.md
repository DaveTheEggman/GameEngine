# PIE on a dedicated game thread (sketch)

Status: SKETCH — a decidable proposal, not scheduled. Separate initiative from the script
debugger (the debugger ships on the suspension model regardless; see
[script-debugger.md](script-debugger.md) §2). Written so "move PIE to a thread" is a real
option to weigh, not a vague someday.

## Goal & payoff
Run the embedded game (Play-In-Editor) on its own thread; keep the editor/UI on the main
thread. Payoff: the editor stays responsive no matter how heavy the game frame is;
headroom for game-side parallelism; and a *blocking-halt* script-debugger mode becomes
available (game thread blocks at a breakpoint, editor thread inspects). Today PIE ticks
synchronously inside the editor loop on the main thread.

## The four pieces (two easy, two risky)

### 1. Game-thread loop + frame handoff — moderate
A game thread owns its run loop (tick scene/physics/scripts/audio at its own cadence). The
editor loop no longer calls the runtime tick; it signals start/stop/pause and reads results.
We already have the worker/job primitives (render extract uses them).

### 2. Render handoff — RISKY (the real work; a render-architecture change, not scripting)
The submit spine is single-threaded and today the game view + editor UI feed one device on
the main thread. Options:
- **(a) Record-on-game / submit-on-main.** Game thread extracts + records command buffers,
  hands them to the main thread to submit. One device, one submit thread; a per-frame
  producer/consumer sync. Keeps submission centralized; couples the two threads each frame.
- **(b) Dual queues.** Game and editor each submit to the device on separate queues. Needs
  real RHI multi-queue support + cross-queue sync; most invasive.
- **(c) Game renders to an offscreen target it owns; editor composites the texture.**
  **Recommended.** The game view is *already* a viewport render target in the editor — make
  that RT game-thread-owned and double-buffered: the game writes buffer N on its thread,
  the editor samples buffer N-1 on the main thread when composing its UI. The two render
  paths meet only at a texture, so there's no shared command stream and no submit-thread
  contention — just a double-buffer + a fence. This is the cleanest isolation and the RT
  boundary already exists. **Do this piece first — it's the enabler and is independently
  useful** (it also decouples game framerate from editor framerate).

### 3. Scene / resource ownership during play — RISKY
The editor reads the scene (inspector, hierarchy, gizmos) on the main thread; a game thread
mutates it → races. Options:
- **(a) Read-only editor + per-frame display snapshot.** During play the scene belongs to
  the game thread; at each frame boundary it publishes a lightweight read-only snapshot the
  editor inspector/hierarchy display from. Edits are disabled (or queued) during play.
- **(b) Ownership transfer.** Scene handed to the game thread on Play; the editor shows a
  frozen last-known view; no live inspection of the running scene.
- **(c) Fine-grained locking.** Avoid — contention + deadlock risk across a big shared ECS.
**Recommended: (a).** It matches how PIE already behaves — you don't live-edit the running
game's authoritative scene (Simulate changes are discarded on Stop anyway), so a read-only
snapshot for display is honest and race-free. The snapshot can be as cheap as the data the
inspector/hierarchy actually read.

### 4. Input handoff — moderate
Shell input arrives on the main thread; the game thread consumes it. We already have an
event-first input layer (a tagged `InputEvent` stream); route it through a thread-safe
queue the game thread drains each tick. Low risk.

## Effort & sequencing
- **Piece 2(c) — offscreen game RT, double-buffered — is the keystone** and is worth doing
  on its own (decouples game/editor framerate even before threading). Build it first.
- Then 1 (game loop) + 4 (input queue) are moderate.
- Then 3(a) (display snapshot) makes the editor safe to read during play.
- Risk concentrates in 2 and 3; both are containable with the recommended options. Overall:
  weeks-scale, touches render + editor-host + input, but no single piece is a research
  problem given the recommendations.

## Interaction with the debugger
Independent. The suspension debugger ships without any of this. If PIE-on-thread later
lands, the debugger *may* add a blocking-halt mode as an alternative — but suspension keeps
working and is arguably still preferable for VM-state inspection. So this initiative is
justified (or not) on **editor responsiveness + parallelism**, never as a debugger
prerequisite.

## Decision
Pursue only if editor-responsiveness-during-play or game-side parallelism becomes a
priority. If pursued, start with 2(c) (the offscreen RT) as a standalone, independently
useful step. Otherwise shelve — the debugger and everything else proceed without it.
