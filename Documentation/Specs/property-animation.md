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

CORRECTION 2026-08-16: the "P2 = BUILD the curve-editor widget" framing below was
STALE - the widget ALREADY EXISTED (`Foundation::UI.Toolkit CurveCanvas`, ported
from Sedulous, tested, used by the particle page). DONE 2026-08-16: the clip page
now hosts a CurveCanvas per scalar track (Float/Float3/Color); Quat keeps the xyzw
key table. The mapping seam (canvas Time is [0,1], interpolation is per-channel
Hermite/Linear/Step) resolved via: keys normalize by an editable clip Length
(tangents rescale with it), a per-track interp cycle maps {Cubic/Linear/Constant}
<-> {Hermite/Linear/Step} and applies to all channels/keys, edits write back live
with one undo per gesture, MaxKeys 8 -> 64. So P1 shipped WITH a curve canvas.
Remaining canvas niceties (true per-KEY interp modes, box-select multi-key ops)
are optional polish, not blocking.

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
  ^^^ STALE (see CORRECTION 2026-08-16 in BUILD STATUS): the widget ALREADY
  EXISTS (UI.Toolkit CurveCanvas). "P2" is now ADOPTING it in the clip page
  (a mapping layer), not building it.
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

## Open design questions for Fable - editing UX / scene-editor extensions (2026-08-16)

RAISED by the user, not yet decided. The runtime + the standalone clip page are
built and staying; this is about whether/how to ADD an in-scene authoring path,
and whether that generalizes to other spatial editors. Fable: add notes inline;
this is a genuine open question, not a proposal to react to.

FIRM CONSTRAINT (user, 2026-08-16): the standalone PropertyAnimationClip editor
page STAYS - it is not being retired. It remains until the user decides it is no
longer necessary. Any in-scene path is IN ADDITION to it, and the two must not
diverge into two separate UIs to maintain.

Context / what exists today:
- The clip is authored in a STANDALONE bespoke page (editor.propertyanimation):
  track list + curve-canvas keying (scalar) / xyzw key table (quat) + a transport
  scrub that shows sampled values. It has no scene - track targets are typed
  component-type + property strings; preview is a value readout, not a live entity.
- We ALREADY have a scene-viewport tool substrate, live-wired into the scene page
  (Editor.ViewportTools): IViewportTool (consumes viewport input + draws an overlay
  into the scene debug-draw + a status line), ViewportToolManager (per-viewport tool
  palette + gesture-safe switching), IViewportToolProvider + registry (domain libs
  contribute tools; explicit registration + count tripwire), and a
  ViewportToolHostContext that hands a tool the Scene* + command stack + entity
  Selection. Today the only tool is SelectTransformTool (the gizmo). The file's own
  comments were written anticipating terrain ("terrain.sculpt", "Editor.Terrain adds
  sculpt + splat", "a terrain brush needs a terrain in the scene").
- GAP: IViewportTool contributes an OVERLAY + STATUS only, not a dockable PANEL.
  Terrain would want a brush-settings panel; property animation the timeline/curve
  panel.

Prior art (surveyed, for calibration - "not to recreate, but to exceed"):
- Lumix: three editor-plugin flavors - IPlugin (owns editor windows), MousePlugin
  (hooks the scene-view mouse; terrain + spline paint IN the real viewport), GUIPlugin
  (owns a dockable panel with onGUI). Terrain/spline editing is in-scene brush + a
  settings panel. Lumix's property_animator editing is weaker (component-property
  curves, no in-scene timeline). Our IViewportTool is a narrower/cleaner MousePlugin
  (consume-flag + a real overlay channel); we have no GUIPlugin-style panel seam yet.
- Sedulous (our prior model): the property-animation asset page EMBEDDED its own
  viewport and let the user pick a prefab/scene document + entities to choose track
  sources, with playback/preview in the same page. It worked; the user's read is the
  UX may not have been great (the preview world is separate from the real scene).

The user's (un-fleshed) idea: "scene editor plugins" - plugins inside the scene
editor that contribute editing modes, gizmos/nodes, and UI pieces. Property
animation could be one such mode (author against the REAL selected entities, preview
in the REAL viewport); terrain and nav mesh could be others. Note the per-feature
shape differs: terrain data lives on a component (in-scene brush, no reusable asset),
nav mesh is baked from scene geometry (mostly in-scene), but a property-animation
clip is a REUSABLE cooked asset that plays on any entity (asset AND in-scene
authoring) - a hybrid the other two are not.

Questions:
1. Is a general "scene-editor extension" abstraction worth introducing (a mode/plugin
   that bundles a viewport tool + a contributed dockable panel + optional gizmos), or
   is the existing IViewportTool + per-page panels enough, wired case by case? What is
   the right SHAPE of the panel-contribution seam - does IViewportTool grow an
   optional panel view, or does the host page own a dock area the tool drives?
2. For property animation specifically: is an in-scene authoring/preview MODE worth
   adding ALONGSIDE the standalone page (which stays)? If yes, how do the two share
   the editing UI so they never diverge - does the in-scene mode HOST the same clip-
   page widgets in a scene-docked panel, or is there a shared editing core both call?
3. Track-source picking + live preview from the live scene: the mode would add tracks
   from the current entity/component selection and preview by driving a clip against
   the selected entity in the real viewport. Any correctness concern doing that while
   the scene may be in a preview/sim state (the UI mutation-queue rule; reflected-
   component access under the generation guard; the animator writing props while a
   sim also runs)?
4. Do terrain + nav mesh genuinely share this system, and if so should the seam be
   designed with all three in mind NOW, or validated incrementally (build the seam
   for one, generalize once a second consumer exists)? Which consumer should prove it?
5. Where does the reusable-asset-vs-scene-local tension land - is it fine for property
   animation to have BOTH an asset page and an in-scene mode while terrain/nav mesh
   have only the in-scene mode, or does that inconsistency argue for one model?
6. Scope check: is this a near-term track, or does it sit behind the current queue
   (navigation, terrain) since the standalone page already ships a working authoring
   path?
