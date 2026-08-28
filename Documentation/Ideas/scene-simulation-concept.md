# A "simulation" concept in the scene layer?

> Status: **RAW THOUGHT, under evaluation.** Not a decision, not ready to build.
> Origin: user, 2026-08-28, mid-PaperKid. Captured before the train of thought
> was fully formed - the benefit is still being reached for.

## The thought (user's words, lightly kept)

Should we move / have a concept of a **simulation** in the scene layer? It would
basically be the GameInstance but from the scene level. The feeling is it would
make **scenes and the instance more connected**. Maybe that's what the scene
*group* already is? Roughly: **1 instance = 1 simulation**, but as something
**scenes can know about**. (The specific benefit being chased got lost - this doc
exists to hold the question until it comes back.)

## What already exists that's adjacent (so we don't reinvent)

The pieces this is circling are all real and were just exercised in PaperKid P1:

- **`GameInstance` is effectively "the simulation / run scope" already.** It owns
  a `SceneManager` (the *scene group*), a run-scoped `EventBus`, an
  `m_instanceTimeScale`, the script run host, and the network controller. A run =
  one GameInstance. So "1 instance = 1 simulation" is close to describing what
  GameInstance IS today.
- **The scene *group* (`SceneManager`) is only PART of it.** The group is the set
  of ticked scenes + a group timescale. The GameInstance wraps that group with
  the run bus, script host, timescale, and net. So "simulation" ≈ the GameInstance
  (the whole run scope), not just the group.
- **Scenes are already "borrowed" by the run, not owners.** The run injects its
  bus into the group (`SetSceneEventBus(&m_runEvents)`) so a **scene's event bus IS
  the run bus** - one bus per run scope. A scene already, quietly, shares run
  state.
- **Scene scripts already REACH UP to the run.** The Level tier is a scene script
  that calls `run::events()` / `run::setTimeScale()` - it wants exactly this
  "scene knows its simulation" connection, and gets it today via an ambient
  per-context `RunScriptBinding` service, not a first-class handle.

So the raw idea may be less "add a new thing" and more "**formalize + name the
run scope that scenes already half-know about**."

## Candidate benefits (prompts to recover the lost train of thought)

None decided - just directions the value *might* have been:

1. **A first-class "my simulation" handle for a scene**, instead of ambient
   wiring. A scene / scene script could reach run-scope services (the shared bus,
   timescale/pause, load-scene, its sibling scenes, net) through one explicit
   `scene.simulation` (or similar) rather than the injected `RunScriptBinding`
   service + the `run::` facade resolving from context.
2. **Untangle the timescale muddle.** We just hit two different "group"
   timescales: `SceneManager.m_timeScale` (scene sim) vs the `FrameTime` group
   slot fed by `m_instanceTimeScale` (the Game script's dt). A single "simulation"
   as the one owner of run time / pause could make that one concept instead of two
   confusingly-named ones (see the run.setTimeScale work + its follow-up note).
3. **Multi-scene runs knowing their siblings.** If a run holds several scenes
   (streamed levels, a persistent HUD scene + a gameplay scene), a scene knowing
   "the other scenes in my simulation" is currently only expressible through the
   group. A named simulation could make sibling/lifecycle relationships explicit.
4. **Editor/runtime symmetry.** PIE runs a GameInstance; the editor ScenePage is
   its own run scope (page bus). "Simulation" as the shared abstraction over both
   run hosts could reduce the "is this the app or the editor" special-casing.

## Likely shapes of a conclusion (either is fine here)

- **"It already exists, just needs a name + a scene->run handle."** The honest
  read so far: GameInstance = the simulation; the only real gap is that scenes
  reach it ambiently rather than through an explicit handle, and the time model is
  double-named. A small formalization, not a new subsystem.
- **"Do nothing."** The ambient wiring works and the coupling is intentionally
  loose (scenes are borrowable across scopes: GameInstance, editor page, tests).
  Making scenes *know* their simulation could re-couple what was deliberately
  decoupled ([[messaging-track]]'s "one bus per scope, scene-borrowed" ruling).

## Open question to answer before this graduates

**What does a scene actually GAIN by knowing about its simulation that it can't do
today through the run facade + the shared bus?** If the answer is "nothing
concrete," this stays a naming/ergonomics note. If a real capability falls out
(sibling-scene access, a unified pause/time owner, cleaner Level-tier ergonomics),
that capability - not the abstraction - is what to spec.

---

### Evaluation trail

**2026-08-28 (Opus, capture):** Mapped the thought against the just-built
GameInstance/run-scope reality (timescale work, scene-bus == run-bus). Initial
lean: this is mostly *naming + a first-class scene->simulation handle* over things
that already exist, with the timescale double-naming as the one concrete wart it
could fix. Not enough of a benefit yet to move to a spec - parked for the user to
refill the "benefit I was looking for" blank.
