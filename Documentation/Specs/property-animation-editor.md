# Property Animation Editor (design)

> STATUS: DESIGN FOR FABLE REVIEW 2026-08-16. Nothing here is built yet. This
> supersedes the "IN-SCENE EDITOR REDESIGN" note in property-animation.md and is
> the authoritative design for the in-scene property-animation editor. The runtime
> + data model (property-animation.md phases A-G) are DONE; this is purely the
> EDITOR. Reference sources farmed locally: Godot
> (`/home/robert/Dev/CPP/godot/editor/animation/animation_track_editor.cpp` +
> `animation_bezier_editor.cpp` + `animation_player_editor_plugin.cpp`), Traktor
> (`/home/robert/Dev/CPP/traktor/code/Ui/Sequencer/*` +
> `code/Theater/Editor/TheaterComponentEditor.cpp` + `code/Core/Math/TransformPath.*`),
> Sedulous (`/home/robert/Dev/Beef/SedulousEngine/Code/Editor/Sedulous.Editor/src/Pages/TimelineView.bf`).

## Goal

A property-animation editor that is genuinely good - better than Sedulous's basic
dopesheet, drawing the best ideas from Godot (the deepest) and Traktor (the
cleanest widget architecture + viewport preview). It keyframes ANY reflected
component property (plus the built-in Transform) over time, scrubs the live scene,
and lets you shape curves. Docked below the viewport, resizable, persistent.

## Reference survey (what we take from each)

- **Godot** - the richest interaction model. TAKE: one time-mapping authority all
  views share; per-lane playhead overlay (cheap scrub); drags-are-visual +
  atomic-commit + reselect-by-time (the anti-glitch discipline); per-key
  interpolation + a bezier sub-editor; box-select; inspector "key" button; two
  snap toggles; ruler tick algorithm. AVOID: the 10k-line god-object; hand-rolled
  hit-test rects for the whole row; rebuild-the-world on every change; scattered
  re-entrancy booleans.
- **Traktor** - the cleanest architecture. TAKE: a DOMAIN-AGNOSTIC timeline widget
  that emits gesture EVENTS and lets the host commit (widget never mutates the real
  model); Act -> Track(entity+property) -> Key model; scrub-poses-the-live-scene
  with physics auto-disabled + a playback->playhead feedback loop; capture-based
  keying; viewport PATH PREVIEW cross-highlighted from timeline selection; Ease
  Velocity / Time-Scale. AVOID: no real ruler; single-select only; key-drag inverts
  order (never re-sorts); no numeric value editor; TCB-only; Euler orientation.
- **Sedulous** - the baseline we are replacing. Its dopesheet (label column + ruler
  + diamonds + draggable playhead + Play/Pause/Stop) is the right SHAPE; its
  interaction has the glitches the user flagged (from the same class Godot's
  discipline in D4 below prevents).

## Current substrate (already built - reuse, do not rebuild)

- Data: `PropertyAnimationClip { f32 duration; Array<PropertyTrack> tracks }`;
  `PropertyTrack { String componentType, propertyPath; TrackValueKind kind;
  Curve channels[kMaxChannels]; Array<QuatKey> quatKeys }`. `Curve` holds
  `CurveKey { time, value, tangentIn, tangentOut; CurveKeyInterpolation
  (Constant/Linear/Cubic) }` - so PER-KEY interpolation + tangents ALREADY EXIST
  (Godot-parity on that axis; Sedulous/Traktor are per-track only).
- Editor seam: `IClipEditorHost` (clip + command stack + dirty + scrub hook) +
  `ClipEditorView` (the shared view). `CurveCanvas` (a working curve/bezier editor
  widget, per channel).
- Runtime + preview: `PropertyAnimatorComponent` + the binding resolver
  (`ResolveBinding`/`WriteBinding`/`ReadBinding`), the built-in "Transform" target
  (RMW via Set/GetLocalTransform), and a working live preview onto the selected
  entity (snapshot/restore, empty-channel-safe via `SampleMerged`).

We are missing the SHELL: a timeline/dopesheet, a real playhead scrubber, a
transport, and viewport path preview. Those are this spec.

## Core architecture decisions (the crux - please review these first)

- **D1 - one Timeline transform authority.** A single object owns time<->pixel
  mapping: `pixelsPerSecond` (zoom), `scrollSeconds` (pan), `labelColumnWidth`.
  Every view (ruler, each lane, playhead, curve view) READS it; none caches its own
  copy. Horizontal scroll/zoom/pan then fall out of one place. (Godot's `Range`
  timeline; Traktor's bug was caching the scale per-row.) Views subscribe to a
  "transform changed" signal rather than a hand-maintained web of redraw calls.
- **D2 - playhead is a cheap overlay.** Scrubbing updates only the playhead's
  draw + the live preview, never a full dopesheet repaint. (Godot per-lane overlay.)
- **D3 - reference keyframes by STABLE IDENTITY, not array index.** A dragged/edited
  key must survive re-sorting without the selection pointing at the wrong key. Two
  options for review: (a) add a `u32 id` to `CurveKey`/`QuatKey` (stable, cheap,
  wire-neutral if not serialized - or serialized for cross-session stability), or
  (b) keep index-based storage but ALWAYS re-derive selection by TIME after any
  mutation (Godot does this; more code, no data change). RECOMMEND (a) - it makes
  D4 nearly free and kills the whole index-thrash bug class. DECISION FOR FABLE.
- **D4 - drags are purely VISUAL; mutation is one atomic undo command at commit;
  reselect by identity/time.** During a keyframe drag NOTHING in the clip changes -
  the widget draws the key at an offset. On release, ONE `ClipEditCommand`
  (before/after snapshot, our existing pattern) applies the move, re-sorts, and the
  selection re-resolves by id (D3a) or time (D3b). This single discipline is what
  eliminates Sedulous's glitches (drag-past-neighbour inverts order; selection
  points at the wrong key mid-drag). A small epsilon gate distinguishes a click
  (select) from a drag (move) so a click never jitters a key.
- **D5 - the timeline is a domain-agnostic widget that emits events.** Following
  Traktor: a reusable `Timeline`/dopesheet widget knows only rows + keys positioned
  in time; it emits `PlayheadMoved`, `KeyDragged(row,key,deltaTime)`,
  `KeySelectionChanged`, `KeyContextMenu`, etc. The host (the animation panel)
  owns the clip and commits every gesture through the command stack. The widget
  never touches `PropertyAnimationClip`. This keeps undo/validation/re-sort in the
  host and makes the widget testable + reusable (a future sequencer/cutscene tier).
- **D6 - scrub poses the live scene, with the guards Traktor has and we lack.**
  We already write sampled values onto the selected entity. ADD: disable physics on
  the previewed entity during scrub (a dynamic body fights authored transforms -
  the documented physics rule); and a playback->playhead feedback path so that
  when the scene plays, the playhead follows (not just editor->scene). Explicit
  "who owns the playhead now" state (Editing | Playing) instead of scattered
  booleans (Godot's re-entrancy lesson).

## Layout

Corrected placement (user 2026-08-16): the editor is a RESIZABLE pane BELOW THE
VIEWPORT ONLY - a vertical SplitView between the viewport and the panel, inside the
viewport column, NOT spanning under the hierarchy or inspector. Because SplitView
enforces a 50px minimum pane, the panel is PERSISTENT (always present, resizable),
which also fixes the review-pass-10 #5 undo-UAF + #6 stale-snapshot by making the
editing view outlive its commands.

```
[ hierarchy | [ viewport  ]                         | inspector ]
             [ ----------- <- draggable splitter --- ]
             [ transport toolbar                     ]
             [ tracklabels | ruler                   ]
             [   Transform | keyframe lanes + playhead]  <- dopesheet
             [   Light     |    (curve view for the   ]
             [             |     selected track below)]
```

- The dopesheet: a LEFT label column (track = "Component.property", with per-track
  controls) and a RIGHT lane area (ruler on top + one keyframe lane per track +
  the playhead). One shared draggable `labelColumnWidth` divider (Godot's
  `name_limit` - a shared int, glitch-free alignment, simpler than nested splitters).
- The curve/bezier view (our `CurveCanvas`) shows the SELECTED track's channels for
  shaping - either inline-expanded under the track row (Godot bezier-per-track) or a
  toggled sub-panel. The dopesheet is the timing/scrub surface; the curve view is the
  value-shaping surface. RECOMMEND: a mode toggle (Dopesheet | Curves) plus
  expand-a-single-track-to-curves, decided in phasing.
- IMPORTANT (Godot pitfall): use REAL child controls for the left-column per-track
  buttons (enable, interp mode, remove) - hand-drawn hit-test rects are brittle (no
  focus/keyboard/tooltips). Hand-draw ONLY the keyframe lane + ruler + playhead.

## Timeline, playhead, scrubbing

- Ruler: labeled ticks using the {1,2,5}x10^n step so labels never collide at any
  zoom (Godot ATE tick algorithm); primary + secondary grid lines. A Seconds/Frames
  toggle (fps field). Zoom = wheel, ANCHORED at the cursor (Godot), plus zoom-to-fit
  (frame all keys) and a zoom slider.
- Playhead: a draggable red line + a head handle in the ruler. Click/drag anywhere
  in the lane area OR the ruler scrubs. Keyboard: Left/Right step by the snap unit;
  Shift = 0.25x precise; jump to prev/next KEY (a distinct shortcut).
- Two INDEPENDENT snap toggles (Godot): snap-the-playhead and snap-dragged-keys are
  separate - you often want key-snap without playhead-snap. Snap unit = a step
  field (fps-compat rounding). Relative snapping preserves the selection's phase.
- Scrub drives preview (D6): playhead time -> sample clip -> WriteBinding onto the
  selected entity (already built), gated to EDIT state.

## Keyframe interaction

- Insert: (a) an "Add key at playhead" per selected track (type-appropriate default,
  seeded from the channel's value at the playhead - NOT 0, per review-pass-10 #2b);
  (b) an inspector "key" button per animatable property that inserts at the playhead
  reading the property's LIVE value (Godot) - batched into ONE undo when keying a
  whole vector; (c) capture-based keying (Traktor): "Key selected entity" snapshots
  all the selected entity's animated properties at the playhead, overwrite-if-near
  else-insert - reuses the move/rotate gizmos as the value editor.
- Select: single click (reverse-order hit test so the topmost diamond wins, Godot);
  Ctrl/Shift aggregate toggle; BOX / rubber-band select across lanes (a selection
  overlay; on release each lane contributes its keys in the rect). Selection is a
  set keyed by (track, channel, keyId) (D3).
- Move: purely visual during drag; atomic commit on release; reselect by identity
  (D3/D4). Multi-select shares one time-offset (relative spacing preserved). Snap
  per the key-snap toggle.
- Numeric edit: selecting key(s) shows time + value(s) + interpolation in an edit
  strip (or pushes them to the Inspector like Godot - decide in phasing). Direct
  numeric time/value entry with clean undo.
- Copy/cut/paste (relative to the playhead), duplicate, delete, box-delete. Clipboard
  stores time/value/interp so paste can retarget a compatible track.
- Value-axis ops worth stealing: Scale-selection (about selection or playhead),
  Ease-velocity (Traktor arc-length redistribution for constant-speed motion),
  Time-scale the whole clip.

## Interpolation + curves

- Per-KEY interpolation already exists (Constant/Linear/Cubic). Surface it per key in
  the curve view (not flattened per-track - fixes review-pass-10 #7 WriteBackTrack
  flattening); keep a per-track DEFAULT for new keys.
- Bezier handles: our `CurveKey` has tangentIn/Out; the `CurveCanvas` already draws
  Cubic tangent handles. Add handle MODES (Free / Mirrored / Balanced / Auto) with
  the SCREEN-SPACE RATIO CORRECTION Godot uses (balanced looks balanced on screen
  regardless of non-uniform time/value pixel scale - ABE:2259; non-obvious, copy it).
- Quaternion tracks: keep the numeric xyzw key table for timing + slerp preview
  (already slerped) - no curve handles for rotation (Traktor's Euler gimbal risk is
  avoided; we store quaternions).

## Track types, property picker, grouping

- Tracks bind (componentType, propertyPath, kind). The built-in "Transform" target
  animates position/rotation/scale (already wired). Add-from-selection already
  enumerates the selected entity's animatable reflected properties (InferTrackKind +
  CollectAnimatableProperties, ResolveBinding-validated) - keep it; ALSO add an
  explicit picker (component -> property, filtered to animatable kinds) like Godot's
  SceneTree->PropertySelector for adding one track deliberately.
- Group lanes by component (Godot node-grouping); a track filter/search box; a
  "show only selected entity's tracks" toggle.

## Transport

- Play / Pause / Stop + Play-from-playhead; loop mode driven by the animator
  COMPONENT's loopMode (clip.loop is dead, removed); a speed field; a current-time
  (frame/seconds) field; DCC conventions (Stop -> 0; Play-from-end replays;
  loop-wrap). Playback auto-advances the playhead each frame (the panel Ticks it);
  during playback manual scrub is ignored until Stop (Godot's early-return).

## Viewport integration (Traktor path preview)

For a Transform position track: draw the position curve as a polyline in the 3D
viewport (sample the clip), plus a gizmo at each keyframe and a moving axis marker
at the playhead pose. Highlight the curve from TIMELINE selection (selected track =
bright). Scrubbing already poses the live entity along it. This is the spatial
feedback that makes motion authoring feel good. (We already have DrawOverlay + the
debug-draw channel; extend it from a single marker to the path + key gizmos.)

## Lifetime / persistence

The panel is a persistent view the scene page owns (retire PropertyAnimationTool +
IViewportTool + the two providers; the H1 tool-panel seam stays parked for future
modal tools like terrain). This fixes #5 (undo command outliving a recreatable
view -> UAF) and #6 (stale preview snapshot) structurally. Re-snapshot on
track-identity change is already designed.

## Asset picker slot (separate, user-requested; enables activation)

Upgrade `AssetPickerSlot` to 3 controls - preview icon / Edit / Clear - plus
drag-drop from the asset browser (type-filtered; wrong type = warning toast/log),
and click-preview reveals the asset in the browser. Edit on a PropertyAnimationClip
opens THIS panel focused on that clip; Edit on other assets opens their page. This
becomes an activation path for the panel alongside a View toggle and auto-open when
selecting an entity that has a PropertyAnimator.

## Explicitly NOT copying

- Godot's monolith (one 10k-line god-object) - keep clean module seams: a Timeline
  transform service, a domain-agnostic dopesheet widget (D5), the host/command
  layer, the curve view.
- Hand-drawn hit-test rects for the whole row (use real controls for the left column).
- Rebuild-every-widget on structural change (incremental row diffing).
- Traktor's single-selection, no-re-sort key drag, missing ruler, Euler rotation.
- Sedulous's mutate-during-drag (the glitch source).

## Phasing (proposed; land each green on both compilers with tests)

- P1 - shell: persistent panel + splitter-below-viewport + transport
  (Play/Pause/Stop + playhead) + the Timeline transform (D1) + a real ruler. Keep
  the existing per-track CurveCanvas rows as the body for now. Fixes #5/#6.
- P2 - the dopesheet widget (D5): domain-agnostic Timeline/dopesheet with lanes +
  keyframe diamonds + draggable playhead + click-to-scrub, event-driven; wire it as
  the primary timing surface with the curve view for the selected track. Box-select,
  drags-are-visual + atomic-commit + reselect-by-id (D3/D4). Snap toggles.
- P3 - keying + polish: inspector "key" button, capture-based keying, copy/paste,
  scale/ease ops, bezier handle modes, viewport path preview, the property picker.
- P4 - asset slot + activation (its own track; can proceed in parallel).

## Open questions for Fable

1. D3: add a stable `id` to CurveKey/QuatKey (recommended), or reselect-by-time only?
   If we add an id: serialize it (cross-session stable) or runtime-only (editor
   session stable, wire-neutral)?
2. Dopesheet vs curve view: one toggled mode (Dopesheet | Curves), or
   expand-a-track-inline to its curves (Godot), or both?
3. Numeric key editing: an in-panel edit strip, or push selected keys to the
   Inspector (Godot's throwaway-object pattern) to reuse the existing property grid?
4. Scope of the reusable Timeline widget: build it engine-generic now (foundation
   ui.toolkit, reusable for a later sequencer/cutscene tier per the Traktor Theater
   shape) or editor-local first and generalize later?
5. Capture-based keying: worth P3, or defer? It reuses the gizmos and is very
   ergonomic but couples the editor to the scene manipulators.
```
