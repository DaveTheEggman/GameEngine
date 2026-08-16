# Property Animation Editor (design)

> STATUS: APPROVED WITH AMENDMENTS (Fable review 2026-08-16) - build in the
> proposed phase order; the rulings on the five open questions and the
> amendment list are at the bottom and are BINDING. Nothing here is built yet.
> This supersedes the "IN-SCENE EDITOR REDESIGN" note in property-animation.md
> and is the authoritative design for the in-scene property-animation editor.
> The runtime + data model (property-animation.md phases A-G) are DONE; this is
> purely the EDITOR. Reference sources farmed locally: Godot
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

## Open questions - Fable rulings (2026-08-16, binding)

1. **D3 key identity: NEITHER option as written - the COMMIT RETURNS THE REMAP.**
   Do NOT add an id field to `CurveKey`/`QuatKey`: `Curve` is a foundation.core
   MATH type and editor-selection identity does not belong in it; and
   reselect-by-time alone is ambiguous because coincident key times are legal in
   our `Curve`. The clean third option: D4 already funnels every mutation
   through one commit, and the commit site is the only place a re-sort happens -
   so the commit applies the move with a STABLE sort and returns the index
   permutation; the host maps the selection set through it. No new data, no
   ambiguity, foundation stays clean. For undo/redo (before/after snapshot
   restores where no permutation exists), re-derive selection by
   time-with-epsilon and accept approximation there - it is the rare path.
   If this gets genuinely messy in P2, the fallback is a RUNTIME-ONLY id
   side-table owned by the editor (never a field on the math type, never
   serialized).
2. **Dopesheet vs curves: the MODE TOGGLE ships in P2; inline-expand is P3
   polish if still wanted.** Hard requirement either way: the curve view MUST
   share the D1 time transform (same zoom/pan/playhead/label-column width) so
   toggling keeps visual context - Godot's bezier editor sharing the time axis
   is the part that matters, not which affordance switches views. This also
   means `CurveCanvas` migrates off its internal normalized 0..1 time domain
   for this use: the widget consumes the shared seconds<->pixels transform
   directly. That kills the re-domain drift class (pass-10: `(x/d)*d` per
   gesture) at the root instead of tolerating it.
3. **Numeric key editing: the in-panel edit strip.** Our inspector is
   entity-bound; Godot's throwaway-object pattern would drag reflection shims
   in for little gain. The strip (NumericFields for time + per-channel value +
   an interpolation dropdown) lives in the panel, works identically whether or
   not an entity is selected, and reuses existing controls.
4. **The Timeline widget goes in `foundation.ui.toolkit` from day one** -
   sibling of `CurveCanvas`, rows + keys + ruler + playhead + selection +
   events ONLY, zero clip knowledge (D5 as written). We already know the next
   consumers (sequencer/cutscene tier, skeletal-animation event tracks), the
   incremental cost is small, and the placement FORCES the domain-agnostic
   seam instead of promising it. It also makes the widget headlessly testable
   in UI.Toolkit.Tests.
5. **Capture-based keying: yes in P3, but decoupled by construction.** Do not
   couple to the gizmos: "capture" = read the entity's current property values
   through the EXISTING binding resolver (`ReadBinding`) at commit time -
   however those values got there (gizmo drag, inspector edit, script). The
   gizmo needs zero knowledge of the animation editor and vice versa; the
   spec's own coupling worry dissolves.

## Fable amendments (binding, additive to the design above)

- **A1 - the standalone clip page is RETIRED.** The spec is silent on the
  two-host story; ruling: the panel is THE editing surface.
  `PropertyAnimationClipEditorPage` + its factory go away in P1 (asset-browser
  "Edit" focuses the panel with the clip loaded, per the asset-slot section);
  entity-unbound editing works in the panel with preview simply disabled.
  KEEP a thin host seam (the IClipEditorHost shape: clip + commands + dirty)
  between panel chrome and editing view so a standalone page can return
  cheaply if ever wanted - but do not build or keep one now.
- **A2 - persistent but COLLAPSIBLE.** The panel view is persistent (that is
  what fixes pass-10 #5/#6 - never destroyed while its commands live), but
  scene editing wants its vertical space back: the pane collapses to the
  transport strip (or a thin restore handle) via a header toggle. Collapse
  hides, never destroys. SplitView's 50px minimum is an implementation detail,
  not the reason for persistence - state the lifetime rule, not the widget
  constraint.
- **A3 - seconds are the only time currency.** Widget events, the D1
  transform, and key storage all speak clip-domain SECONDS; there is no
  normalized layer anywhere in the new surface (see ruling 2). The
  Seconds/Frames toggle is a display/snap format only.
- **A4 - transport loop is an editor-local toggle**, DEFAULTED from the
  selected animator component's loopMode when bound. Preview looping must not
  depend on which entity happens to be selected once the user has set it, and
  unbound clip editing needs a loop control too. (clip.loop stays dead.)
- **A5 - theming + design system.** All new chrome resolves from the theme:
  the widget follows the CurveCanvas precedent (background/border/dim-text via
  background-color/border-color/text-dim-color + registered type names for
  both the Timeline widget and the panel); the playhead + selection colors
  resolve AccentColor (playhead may prefer error-red - resolve ErrorColor,
  fall back red); transport/toolbar text is 12px compact chrome per the ramp.
  No hand-picked hex constants in draw code - fallbacks only.
- **A6 - damage-gate producers.** Scrub and playback self-chain damage
  (MarkNeedsRedraw per frame while active); an idle panel contributes ZERO
  redraws. Playhead-only updates must not repaint lanes (D2 is the mechanism -
  state the gate contract explicitly so it is tested, not hoped).
- **A7 - undo inventory.** One command per user gesture, including multi-key
  box drags, paste, scale-selection, ease ops, and whole-vector inspector
  keying (the spec has this); discrete repeatable actions (add-track,
  add-key-at-playhead) take BeginGroup+LockGroup so consecutive invocations
  never coalesce (pass-10 #7 lesson). Commands hold clip snapshots + the host
  seam, NEVER view pointers (pass-10 #5).
- **A8 - test inventory (the seam checks for THIS surface, written with each
  phase, not after).** Widget (UI.Toolkit.Tests, headless): time<->pixel
  transform round-trip incl. zoom-anchor math, ruler tick algorithm
  ({1,2,5}x10^n never collides), hit-test topmost-wins, box-select set math,
  snap math (both toggles, relative phase), event emission per gesture, and
  drag-is-visual (no model mutation before commit - the widget has no model to
  mutate, assert the events carry deltas only). Host (Editor tests): one
  undo step per gesture incl. box drag; commit remap preserves selection
  across re-sort; per-key interpolation survives edit round-trips (the
  flattening regression); add-key seeds from the channel value at playhead,
  never 0; preview snapshot lifecycle across track-set changes; transport
  state machine (Editing|Playing ownership, scrub ignored while playing).
  Keep the four original canvas checks green (Constant staircase, no-edit
  byte-identity - now exact per A3, one-undo-per-gesture, MaxKeys).
- **A9 - track lanes scale.** Label column + lane area share ONE vertical
  scroll; rows are cheap (no per-row allocations per frame) and the structural
  rebuild path diffs rows incrementally (the spec's own "not copying" list) -
  virtualize only if a real clip shows the need, but keep the row model
  virtualization-shaped (flat array of row records, not a nest of ad hoc
  children).
- **A10 - keyboard map (P2 minimum).** Space = play/pause, Home/End = clip
  start/end, Left/Right = step by snap (Shift = 0.25x), Ctrl+Left/Right =
  prev/next key, K (or Insert) = key-at-playhead for selected tracks, Delete =
  delete selected keys, Ctrl+C/X/V = clipboard, Ctrl+A = select all keys in
  visible tracks. Document deviations in the spec when implementation forces
  them.

Review pass cadence: each phase lands green on both compilers with its A8
slice; Fable reviews per phase against this spec (the pass-10 required fixes
1-3 and 7-8 on the RUNTIME side proceed independently and are not blocked on
this editor).
