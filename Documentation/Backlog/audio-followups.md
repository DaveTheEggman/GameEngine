# Audio - grain banks + growth path

> Status: CURRENT
> Track: [[audio-subsystem]]

Audio is the most complete subsystem; the ONE remaining north-star item is Traktor-style grain banks.
Decided with the user (2026-07-19): do NOT port them big-bang. Rationale: (1) the authoring model and
especially the tree-editor UI would be designed in a vacuum until a real game/demo pulls on specific
behaviors; (2) the highest-leverage engine work is script-driven gameplay, which is exactly what will
surface real grain requirements; (3) the shipped `SoundCue` model grows into grains INCREMENTALLY,
each step small and independently useful.

Traktor's grains are a compositional sound-graph (sequence / random / repeat / simultaneous / envelope
/ blend / in-loop-out nodes) - FMOD-event-lite as data, leaning on a game-parameter system we do not
have yet.

## Incremental growth path (from `SoundCue`)

- **a. In-loop-out on cues** (intro clip -> sustain loop -> tail on stop) - the most-wanted behavior;
  the first bite when audio work resumes.
- **b. Parameter system** (`Audio.setParameter("rpm", v)`) - the genuinely new primitive everything
  else hangs off.
- **c. Blend cues** (crossfade variants by a parameter - engine-RPM layers).
- **d. Sequence / composite cues** (cues nesting cues) - at which point we effectively HAVE grains,
  grown rather than ported.

At step (d) the grain-bank feature is reached by growth, not a big-bang port; the tree-editor UI is
designed against real content by then.
