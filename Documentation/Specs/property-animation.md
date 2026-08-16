# Property animation

**Status:** BUILT 2026-08-16 (all 7 phases). First of the three parity P0
tracks (this, then navigation.md, then terrain.md - see
docs/design/parity-2026-08.md). Prior art: Lumix `property_animator`
(reflected float curves); ours is multi-type from day one (user decision)
because Variant + reflection make it nearly free and transform animation
(Float3/Quat) is the number one use case.

## BUILD STATUS (2026-08-16)

All phases shipped, green on clang + gcc:
- **A** foundation.core:curve - scalar keyframe curve (Constant/Linear/Cubic
  Hermite). 36 assertions.
- **B** foundation.propertyanimation - TrackValueKind{Float,Float3,Quat,Color},
  PropertyTrack (per-component Curves + slerped QuatKeys), Sample->Variant, the
  reflection binding resolver (nested paths, live-re-walk write). 37 assertions.
- **C** foundation.propertyanimation.resource - cooked clip (flat SoA source +
  FromClip/FillClip, bounds-guarded) + factory (model-A). 54 assertions.
- **D** propertyanimation.pipeline - PropertyAnimationClipAsset embeds the source
  + builder; registered in Pipeline::Registration (kBuilderCount 21->22). Cook
  round-trip + tripwire tests.
- **E** engine.animation:propertyanimator - PropertyAnimatorComponent + per-scene
  manager (injected by AnimationSubsystem, ticks PostUpdate): Once/Loop/PingPong
  clock, per-track binding built once (component-by-type-NAME + property chain),
  live re-resolve write, failed-track disable-with-one-warning. 43 assertions.
- **G** scripting - reflected play/stop/pause/resume/isPlaying/time/setTime via
  the existing `.of(entity)` surface (no facade lib). 52 assertions.
- **F** editor.propertyanimation - the P1 clip page (track list + keyframe table +
  transport scrub + toolbar undo) + New Asset creator, wired into the editor exe.

REMAINING (follow-ups, not blocking the feature):
- Live preview writing to a SELECTED scene entity (needs the cross-page selection
  seam; the page's scrub shows sampled values today).
- The physics-kinematic-rule TEST (documented in the component; the cross-subsystem
  test rides the physics manager).
- Acceptance UAT: the demo scene (animated position + light color, looping) in
  play-in-editor AND the exported player + a user visual pass.
- P2 (separate): the curve-editor widget in ui.toolkit.

GOAL: animate ANY reflected property on ANY component with keyframe curves,
as data (a clip asset) driven by a component - no code per animated thing.
This is also the substrate for a later sequencer/cutscene tier (Traktor
Theater shape: acts of property tracks over time) - design nothing that
blocks that, build none of it now.

## Module layout (the library shape)

NOT an extension of Foundation/Animation - that module is skeletal (bones,
poses, skinning deps). Property animation depends on foundation.core
reflection ONLY, and that independence is the point: headless consumers
(editor page, tests, MCP hosts) must never pull skeletal machinery to
evaluate a property clip. Dotted-suffix naming would lie about the
dependency (Animation.Property reads as "part of Animation"), so it gets
its own name. The full triad, mirroring the Particles/Physics pattern:

- `Code/Foundation/PropertyAnimation` - module
  `foundation.propertyanimation`, alias `Foundation::PropertyAnimation`.
  The clip/track/curve data model, evaluation, and the reflection binding
  resolver (resolves against TypeRegistry/Instance - no scene dependency).
  Links Foundation::Core only.
- `Code/Foundation/PropertyAnimation.Resource` - module
  `foundation.propertyanimation.resource`. The cooked runtime resource +
  loader (model-A pattern, same as Animation.Resource/Texture.Resource).
- `Code/Pipeline/PropertyAnimation.Pipeline` - the source asset
  (PropertyAnimationClipAsset, Pipeline domain), XML->binary builder, New
  Asset creator. Registered via Pipeline::Registration (bump kBuilderCount
  + tripwire).
- Engine side: EXTEND `Engine/Engine.Animation` - PropertyAnimatorComponent
  + PropertyAnimatorComponentManager (house pattern: scene::
  SerializableComponentManager<T> IS the per-scene system - storage +
  lifecycle + serialization + the playback tick in one class, like
  SkeletalAnimationComponentManager), injected into scenes by the existing
  AnimationSubsystem; the DefaultApp/registration surface is unchanged.
  Engine-side coupling is acceptable where foundation-side coupling was
  not: a game with the animation subsystem wants both.
- `Code/Editor/Editor.PropertyAnimation` - the clip page (per-domain editor
  lib precedent: Editor.Physics, Editor.Audio, Editor.Input).
- Curve math (Sedulous Curve/CurveKey port) lands in foundation.core Math -
  it is general math per the gap policy, shared with any future consumer
  (skeletal can migrate to it later); NOT private to this module.
- Tests: PropertyAnimation.Tests + PropertyAnimation.Resource.Tests +
  builder cases in the pipeline test dir, per the module-test rule.

## Data model

- **PropertyAnimationClip** (source asset, XML text like scenes; cooked to
  binary product through the usual triad):
  - duration, loop hint
  - tracks[]; each track:
    - target: component type name (string, resolved via TypeRegistry) +
      property PATH (dot-joined for Nested properties, e.g.
      "light.color" - reflection already walks nested)
    - value kind: Float | Float3 | Quat | Color (v1 set; Bool/enum STEP
      tracks are a later addition, the wire format reserves the kind byte)
    - curve data: Float/Float3/Color = per-component keyframe curves
      {time, value, interpolation}; Quat = quaternion keys with slerp
      (NEVER per-component euler curves)
- Interpolation per key: Constant | Linear | Cubic (tangents). CURVE MATH:
  port from Sedulous first per the math gap policy - Sedulous has the
  XNA-heritage Curve/CurveKey; evaluate it before writing anything new.
  The existing skeletal Sampler/Easing in foundation.animation is the
  second source to check.

## Instance model (clarified 2026-08-10)

One PropertyAnimatorComponent per entity (value-pool rule), holding ONE
clip - so P1 = one clip playing per entity at a time (script-swappable;
LAYERING two clips on one entity is out, and the P2 shape for it is a
clip LIST on the component, not multiple components). Tracks target the
OWNING entity's components only - that is what keeps clips reusable
across entities ("pulse" plays on any entity with a light); multi-entity
orchestration is the sequencer's job later, not this component's. Cooked
clips are immutable SHARED data; each playing component owns only its
{time, play state, binding cache}. The manager is PER-SCENE (injected by
AnimationSubsystem), so per-scene time/time-scale/pause apply with zero
special-casing.

## Runtime

- **PropertyAnimatorComponent** {clip Ref, autoplay, speed, loop mode
  (Once | Loop | PingPong), playing state}. displayName + category
  attributes (editor-polish rule); the clip Ref needs its InspectorView
  ref-picker dispatch entry or NO picker renders (known trap).
- PropertyAnimatorComponentManager ticks playback in scene update BEFORE
  render extraction (same phase family as skeletal). Per-instance binding
  cache:
  - Resolve at play start: component by type on the owning entity ->
    PropertyInfo chain by cached hash path. A track that fails to resolve
    is DISABLED with one warning (never per-frame log spam), the clip
    keeps playing its other tracks.
  - Re-resolve on structural change via the existing mutation-generation
    guard (the entity.get lesson: never cache raw component pointers
    across structural changes - sparse-set swap-remove corrupts).
  - Writes go through reflection set (Variant) - so RESOLVE-mode rules
    and any property setter side effects apply exactly as if the
    inspector wrote the value.
- GOTCHA to spec-test: animating the transform of a physics-owned entity.
  Rule: the animator writes the transform like any other property; a
  DYNAMIC rigid body on the same entity wins (physics sync overwrites) -
  document that animated physics entities should be kinematic. Add the
  test that proves the documented behavior.
- Scripting: NO new facade lib. The component's reflected methods
  (play/stop/pause, speed, time) reach scripts through the existing
  reflected-component surface; write them with natural types (i32/f32) per
  the facade-numerics rule. Facade name count unchanged.

## Editor

- P1 ships a PROPERTY ANIMATION PAGE (bespoke-pages recipe): track list
  (add track = component-type picker + property picker filtered to
  animatable kinds), keyframe TABLE editing (time + value + interp per
  key), transport (play/scrub) with live preview on a selected entity in
  the scene page. NO curve canvas in P1.
- P2 (separate, after P1 proves the data model): the curve editor widget
  in ui.toolkit (multi-curve canvas, tangent handles, box select). It is
  real toolkit work - do not let it block the runtime shipping.
- Preview must respect the UI mutation-queue rule and write through the
  undo path when editing keys (one undo step per key operation).

## Pipeline

- New Asset creator (no OS-file importer - clips are authored in-editor).
- Builder cooks XML source -> binary product; bump kBuilderCount and the
  Pipeline.Registration tripwire test. Product registered for the player.
- Types: rtti::pipeline domain for the asset, Runtime domain for the
  component + cooked clip (they SHIP in the player - unlike authoring
  assets).

## Tests (required, per house rule)

- Curve sampling: exact key hits, between-keys lerp/cubic, Constant
  stepping, loop/pingpong time wrapping, quat slerp shortest-arc.
- Binding: resolve nested paths; missing component; missing property;
  renamed property (fails disabled, no crash); structural-change
  re-resolve under the generation guard.
- Scene integration: a clip drives Float3 position + Color light over a
  simulated tick sequence; values sampled at t match expected.
- Physics interaction test proving the documented kinematic rule.
- Wire: XML source round-trip + cooked product round-trip (count-guard on
  tracks/keys, wire-symmetry lesson).
- Tripwires: builder count; inspector picker dispatch entry present.

## Acceptance

Battery green both compilers incl. the new suites; a demo scene (one
entity: animated position + light color, looping) runs in play-in-editor
AND in the exported player; the clip asset survives export; user visual
pass on the demo.

## Explicitly deferred

- Sequencer/timeline (multi-clip, multi-entity orchestration) - later
  track over this substrate.
- Bool/enum step tracks, events-on-keyframe (the bus makes this natural
  later), clip blending, curve editor widget (P2 above).
