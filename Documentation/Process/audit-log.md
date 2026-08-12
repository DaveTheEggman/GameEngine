# Documentation audit log

> Status: CURRENT
> Track: documentation-system (Documentation/Specs/documentation-system.md)

Append-only record of the docs reorganization (spec: `Specs/documentation-system.md`).
A reviewer can reconstruct every move from here. State guesses are PROVISIONAL - P1
classifies finally and verifies Systems claims against code.

---

## P0 - track first, judge later (2026-08-11)

Reverses the docs-never-committed policy: every former `docs/` file plus the root
handoffs/review is now under `Documentation/`, provisionally categorized WITHOUT
content edits, and committed so nothing more can be lost.

- Created `Documentation/{Systems,Specs,Plans,Backlog,Guides,Process,Archive}/`.
- Removed the 4 doc entries from `.gitignore` (they become tracked):
  `Documentation/Planning/Core.md`, `docs/design/{shaders-materials-hot-reload,runtime-host,renderer}.md`.
- Emptied + removed `docs/` and `Documentation/Planning/`.
- `Documentation/Planning/Core.md` -> `Plans/Core.md` (user ruled 2026-08-11: LIVING planning, not historical).
- NOT touched (stay as-is): `KNOWN_ISSUES.md`, `README.md` at root; the user's `start.txt`,
  `net-demo-scripts.txt`, `smoke-test-checklist.txt`.
- Name collisions disambiguated (two files shared a basename across the old design/ + specs/):
  - `docs/design/editor-polish.md` -> `Backlog/editor-polish-design.md`
  - `docs/specs/editor-polish.md`  -> `Backlog/editor-polish-spec.md`
  - `docs/design/reflection-track.md` -> `Systems/reflection-track.md`;
    `docs/specs/reflection-track.md` -> `Specs/reflection-track.md` (different categories - no rename).

State: `current` (believed true/active) | `stale` (likely outdated / track complete -> Archive in P1)
| `unknown` (needs P1 audit) | `archived` (historical, header-only in P1).

### Systems/ (subsystem reference - all `unknown` until P1 verifies claims vs code)

| New path | Old path | State |
|---|---|---|
| Systems/asset-pipeline.md | docs/design/asset-pipeline.md | unknown |
| Systems/audio.md | docs/design/audio.md | unknown |
| Systems/code-editor.md | docs/design/code-editor.md | unknown |
| Systems/editor.md | docs/design/editor.md | unknown |
| Systems/editor-jobs.md | docs/design/editor-jobs.md | unknown |
| Systems/export.md | docs/design/export.md | unknown |
| Systems/export-reachability.md | docs/design/export-reachability.md | unknown |
| Systems/export-templates.md | docs/design/export-templates.md | unknown |
| Systems/game-instance.md | docs/design/game-instance.md | unknown |
| Systems/game-ui.md | docs/design/game-ui.md | unknown |
| Systems/gui-port.md | docs/design/gui-port.md | unknown (experimental/parked) |
| Systems/input.md | docs/design/input.md | unknown |
| Systems/instanced-mesh.md | docs/design/instanced-mesh.md | unknown |
| Systems/networking.md | docs/design/networking.md | unknown |
| Systems/particles.md | docs/design/particles.md | unknown |
| Systems/particles-authoring.md | docs/design/particles-authoring.md | unknown |
| Systems/path-type.md | docs/design/path-type.md | unknown |
| Systems/physics.md | docs/design/physics.md | unknown |
| Systems/post-processing-config.md | docs/design/post-processing-config.md | unknown |
| Systems/prefabs.md | docs/design/prefabs.md | unknown |
| Systems/project-and-settings.md | docs/design/project-and-settings.md | unknown |
| Systems/reflection-probes.md | docs/design/reflection-probes.md | unknown |
| Systems/reflection-track.md | docs/design/reflection-track.md | unknown |
| Systems/renderer.md | docs/design/renderer.md | unknown (was .gitignored) |
| Systems/runtime-host.md | docs/design/runtime-host.md | unknown (was .gitignored) |
| Systems/script-debugger.md | docs/design/script-debugger.md | unknown |
| Systems/scripting.md | docs/design/scripting.md | unknown |
| Systems/settings.md | docs/design/settings.md | unknown |
| Systems/shaders.md | docs/design/shaders.md | unknown |
| Systems/shaders-materials-hot-reload.md | docs/design/shaders-materials-hot-reload.md | unknown (was .gitignored) |
| Systems/text-scenes.md | docs/design/text-scenes.md | unknown |
| Systems/viewport-input.md | docs/design/viewport-input.md | unknown |
| Systems/web-platform.md | docs/design/web-platform.md | unknown |
| Systems/skinning-benchmark.md | docs/skinning-benchmark.md | unknown |

### Specs/ (active build specs - completed tracks move to Archive in P1)

| New path | Old path | State |
|---|---|---|
| Specs/luau-backend.md | docs/specs/luau-backend.md | current (active track) |
| Specs/mcp-agent-access.md | docs/specs/mcp-agent-access.md | current (active track) |
| Specs/documentation-system.md | docs/specs/documentation-system.md | current (this spec) |
| Specs/game-ready-scripting.md | docs/specs/game-ready-scripting.md | stale (track ~complete) |
| Specs/game-ready-scripting2.md | docs/specs/game-ready-scripting2.md | stale (track ~complete) |
| Specs/reflection-track.md | docs/specs/reflection-track.md | stale (task #110, done) |
| Specs/scene-scripting.md | docs/specs/scene-scripting.md | stale (shipped) |
| Specs/sdl-static.md | docs/specs/sdl-static.md | stale (task #119) |
| Specs/gui-tests-keyframes.md | docs/specs/gui-tests-keyframes.md | stale (task #115) |
| Specs/smoketest-fixes.md | docs/specs/smoketest-fixes.md | stale (weekend pass) |
| Specs/camera-preview.md | docs/specs/camera-preview.md | unknown (task #118) |
| Specs/async-resource-loading.md | docs/specs/async-resource-loading.md | unknown (task #123) |
| Specs/source-path-p3-p4.md | docs/specs/source-path-p3-p4.md | unknown |
| Specs/settings-unknown-section-passthrough.md | docs/specs/settings-unknown-section-passthrough.md | unknown |
| Specs/property-animation.md | docs/specs/property-animation.md | unknown |
| Specs/README.md | docs/specs/README.md | current (specs index) |

### Plans/ (approved designs / future phases)

| New path | Old path | State |
|---|---|---|
| Plans/Core.md | Documentation/Planning/Core.md | current (user: living) |
| Plans/roadmap.md | docs/design/roadmap.md | stale (known-stale; P2 refresh) |
| Plans/renderer-improvements.md | docs/design/renderer-improvements.md | unknown |
| Plans/pie-on-thread.md | docs/design/pie-on-thread.md | current (shelved sketch) |
| Plans/navigation.md | docs/specs/navigation.md | unknown (not started) |
| Plans/terrain.md | docs/specs/terrain.md | unknown (not started) |
| Plans/instanced-crowds.md | docs/instanced-crowds.md | unknown |

### Backlog/ (deferred / queued / triage)

| New path | Old path | State |
|---|---|---|
| Backlog/deferred-by-design.md | docs/specs/deferred-by-design.md | current |
| Backlog/issues-triage.md | docs/specs/issues-triage.md | current |
| Backlog/vg-quality-leftovers.md | docs/specs/vg-quality-leftovers.md | unknown |
| Backlog/web-remainder.md | docs/specs/web-remainder.md | unknown (task #112) |
| Backlog/editor-pages-gap.md | docs/design/editor-pages-gap.md | unknown |
| Backlog/editor-polish-design.md | docs/design/editor-polish.md | unknown |
| Backlog/editor-polish-spec.md | docs/specs/editor-polish.md | unknown |
| Backlog/gui-gaps.md | docs/design/gui-gaps.md | unknown |
| Backlog/parity-2026-08.md | docs/design/parity-2026-08.md | current (2026-08-10 census) |
| Backlog/sedulous-backport.md | docs/sedulous-backport.md | unknown |

### Guides/ (how-to)

| New path | Old path | State |
|---|---|---|
| Guides/adding-facades.md | docs/design/adding-facades.md | current |
| Guides/emscripten-windows.md | docs/emscripten-windows.md | unknown |
| Guides/smoke-checklist.md | docs/smoke-checklist.md | unknown |

### Process/ (working agreements)

| New path | Old path | State |
|---|---|---|
| Process/CONVENTIONS.md | docs/specs/CONVENTIONS.md | current |
| Process/HANDOFF.md | docs/specs/HANDOFF.md | current (review baselines) |
| Process/code-standard.md | docs/design/code-standard.md | current |
| Process/audit-log.md | (new, this file) | current |

### Archive/ (historical - header-only in P1, non-authoritative)

| New path | Old path | State |
|---|---|---|
| Archive/handoff-luau-debugger.md | handoff-luau-debugger.md (root) | archived |
| Archive/handoff-reachability-p1.md | handoff-reachability-p1.md (root) | archived |
| Archive/handoff-scripting-capabilities.md | handoff-scripting-capabilities.md (root) | archived |
| Archive/handoff-scripting-p2.md | handoff-scripting-p2.md (root) | archived |
| Archive/handoff-shaders-web.md | handoff-shaders-web.md (root) | archived |
| Archive/review-14f16a84-HEAD.md | review-14f16a84-HEAD.md (root) | archived |

Total: 79 files (72 former docs/ + 6 root handoffs/review + Core.md).

---

## P1 - batch 1: scripting subsystem (2026-08-11)

Verified against code @ 9c9046f8.

- **Systems/scripting.md** REWRITTEN present-tense (was `Status: PLANNED / "Wren = first
  backend"` design+survey doc, heavily drifted - claimed PLANNED while most shipped, used
  the old `draconic.*` module names). Now a CURRENT subsystem reference: the neutral
  contract, THREE backends (Wren/AngelScript/Luau) + gating, the three tiers + run host,
  behaviors/properties/events/coroutines, facades, cook + BYTECODE CONSUMPTION, the AS+Luau
  debuggers, editor. Verified: module names (foundation.script.*), PROFILE_SCOPE,
  ScriptRunHost/ScriptComponent/ScriptSceneSystem, 3 registered backends. State: current.
- SPLIT the mixed doc (spec rule): the reference survey + resolved open-questions + addendum
  + Traktor findings -> **Archive/scripting-design-history.md** (ARCHIVED header, names the
  Systems doc as superseding; full original preserved in git @ 3b92560d). The deferred
  follow-ups (§9) -> **Backlog/scripting-followups.md** (current).
- State updates: Systems/scripting.md unknown -> current (verified). New: Archive/
  scripting-design-history.md (archived), Backlog/scripting-followups.md (current).

NEXT P1 batches (Systems for active subsystems): script-debugger.md, then pipeline/MCP
(mcp-agent-access is a Spec, but the MCP Systems reference needs writing), fonts, renderer,
editor.

## P1 - batch 2: script-debugger (2026-08-11)

Verified against code @ 9c9046f8.

- **Systems/script-debugger.md** REWRITTEN present-tense (was `Status: DESIGN, for review` -
  a plan that predated the shipped debuggers and never mentioned Luau). Now a CURRENT
  reference for the SHIPPED step debugger: the neutral contract (IScriptDebugger / states /
  snapshot types / battery section), run-host game-pause, both backends (AngelScript context
  suspension + Luau pooled-thread lua_break, with the off-by-one + C-boundary nuances), Wren
  = none, bytecode-loaded classes stay debuggable, capture, and the editor UI
  (gutter/DebuggerPanel/execution-line/hover). Verified: AngelScriptDebugger + LuauDebugger
  exist; IScriptDebugger/ScriptDebuggerState/ScriptStackFrame/ScriptVariable; ScriptEditorPage/
  DebuggerPanel/DebugPauseTracker/IsDebugPaused/RequestDebugger/ScriptExecutionPoint/
  HoverValueProvider; NO CreateProfiler impl (profiler unbuilt). State: unknown -> current.
- SPLIT: the unbuilt profiler + remote-transport design (the A/B transport decision, P2-P4
  phasing, open questions) -> **Plans/script-debugger-remote.md** (DRAFT - approved design,
  not started). New: Plans/script-debugger-remote.md.

## P1 - batch 3: asset pipeline (2026-08-12)

Verified against code @ 9c9046f8.

- **Systems/asset-pipeline.md** REWRITTEN present-tense (was "editor phase 6", `Status: LOCKED
  ... Remaining: UX pass, ...`, old `draconic.editor` module names). Now a CURRENT reference:
  content DBs (foundation.content), the recipe-hash/content-hash staleness model + read-vs-
  reference deps, builders (pipeline.core, IAssetBuilder/BuilderRegistry), CookDriver
  (pipeline.cook, DAG-on-JobSystem + orphan sweep), VFS integration (foundation.vfs,
  IStatFileSystem + watchable NativeFileSystem), the resource::Ref<T> reference layer
  (foundation.resource, ResourceManager + ResolveSceneResources), and the surfaces (CLI +
  EditorCookService + AssetsView + drop-file import). Verified: CookDriver, ContentDatabase,
  BuilderRegistry, IAssetBuilder, Instance/Group, EditorCookService, AssetsView,
  ResourceManager, resource::Ref, ResolveSceneResources, ImporterRegistry, IWatchableFileSystem/
  IChangeSource; recipeHash/AssetDependencies/ScanDependencies/orphan in the cook code. State:
  unknown -> current.
- SPLIT: the Lumix/Traktor/Sedulous reference synthesis + the 6a-6d build-order record ->
  **Archive/asset-pipeline-design-history.md** (ARCHIVED). Deferred (thumbnails, platform
  variants) kept as a short section in the Systems doc pointing to Backlog.

## P1 - batch 4: networking (2026-08-12)

Verified against code @ 9c9046f8.

- **Systems/networking.md** REWRITTEN present-tense (was `Draconic - Networking (design)`,
  IMPLEMENTED-but-design-framed, old `draconic.net.*` names, 542 lines with a not-started P3
  sketch + backlog inline). Now a CURRENT reference: the modules (foundation.net transport /
  foundation.net.manager per-instance NetworkManager / foundation.net.replication /
  engine.net NetworkSubsystem), roles + startup, StateReplication (Replicated fields, snapshot/
  per-peer delta, prefab net-spawn + late-join, interpolation, relevancy/fog-of-war), the
  IReplicationModel seam + determinism caveat, the Net script facade, deferred. Verified:
  foundation.net{,.manager,.replication} + engine.net modules; StateReplication, IReplicationModel,
  NetworkStartup, NetworkRole, Replicated, NetworkManager, NetworkSubsystem, NetworkComponentManager,
  SimDatagramNetwork, INetworkController, RegisterNetScriptFacade. State: unknown -> current.
- SPLIT (spec rule): the P3 commands/orders SKETCH -> **Plans/networking-commands.md** (DRAFT, the
  4 slices); the §9 refinements/backlog + validation debt -> **Backlog/networking-followups.md**; the
  transport/genre/sockets rationale + references -> **Archive/networking-design-history.md**.

## P1 - batch 5: physics (2026-08-12)

Verified against code @ 33f64a02.

- **Systems/physics.md** REWRITTEN present-tense (was `Draconic Physics - Jolt-backed subsystem
  (design)`, SHIPPED-but-design-framed, 279 lines with the full reference survey + open questions +
  parked list inline, old `draconic.physics.*` names). Now a CURRENT reference: the real modules
  (foundation.physics / foundation.physics.resource / physics.pipeline / engine.physics /
  editor.physics), the value-pool components as SHIPPED, per-scene settings + collision-group matrix,
  fixed-step + interpolation, queries/events, cooked resources, editor, the ScenePhysics facade.
  Verified against code: modules above; PhysicsWorld + ShapeKind{Box,Sphere,Capsule,Cooked,Plane} +
  ShapeDesc (foundation.physics); RigidBodyComponent/ColliderComponent/CharacterComponent/
  JointComponent + JointKind{Fixed,Point,Hinge,Slider,Distance} + motorEnabled/motorTargetVelocity/
  motorLimit; PhysicsSettings scene component (gravity + groupNames/groupCollides matrix + debugDraw);
  collisionGroup u8; OnFixedUpdate + MoveKinematic + ApplyInterpolation(lerp/Slerp); CollisionShape
  (foundation.physics.resource) + PhysicalMaterial (physics.pipeline) cook kinds ConvexHull/
  TriangleMesh; ScenePhysics.of(scene) facade (rayCast/hitX../gravityY/setGravity/applyImpulse);
  CharacterComponent.move/jump; collision matrix editor in Editor.Scene InspectorView
  (CollisionMatrixTests). State: unknown -> current.
- CORRECTED drift vs the design doc: single ColliderComponent (not per-primitive shape components),
  single JointComponent+kind (not five), groups in scene settings (not project settings), character
  control now scriptable (old doc marked it BLOCKED). No cylinder shape; no convex decomposition.
- SPLIT (spec rule): the reference survey (Sedulous/ez/Godot/Flax) + no-abstraction-theater rationale
  + layer-model choice + deviations-and-why -> **Archive/physics-design-history.md** (ARCHIVED); the
  §10 parked items (convex decomposition, gravity volumes, JPH_DEBUG_RENDERER, per-world job-pool
  consolidation) + the now-resolved per-entity-scripting note -> **Backlog/physics-followups.md**.

## P1 - batch 6: audio (2026-08-12)

Verified against code @ 51b22d7f.

- **Systems/audio.md** REWRITTEN present-tense (was `Draconic Audio - miniaudio-backed subsystem
  (design)`, SHIPPED-but-design-framed, 233 lines with the full reference survey + phasing + open
  questions + a giant status-header changelog inline, old `draconic.audio.*` names). Now a CURRENT
  reference: the real modules (foundation.audio / foundation.audio.resource / audio.pipeline /
  engine.audio / editor.audio), the fixed four-bus AudioBus topology + BusLayout data + effect kinds,
  voices + stealing + faded steal, 3D + reverb zones + multi-listener, cooked resources (AudioClip /
  SoundCue / BusLayout), scene integration + the Audio facade. Verified against code: modules above;
  AudioEngine + VoiceStatus.cursorSeconds + AudioBus{Master,Effects,Music,UI} +
  AudioBusEffectKind{None,Lowpass,Highpass,Delay,Reverb} + Freeverb/AudioReverbParams (foundation.
  audio); AudioClip + BusLayout (foundation.audio.resource); AudioClipAsset + SoundCue asset
  (audio.pipeline); AudioSubsystem + AudioSourceComponent + AudioListenerComponent +
  AudioReverbZoneComponent + AudioUserSettings (engine.audio); AudioClipPage/SoundCuePage/BusLayoutPage
  (editor.audio); facade methods playOneShot/playOneShot3D/playCue/playMusic/stopMusic/busVolume/
  setBusVolume; listenerCount 1..4. State: unknown -> current.
- SPLIT (spec rule): the reference survey (Sedulous/Godot/Traktor/Lumix) + no-abstraction-theater +
  Null-mode + backend rationale + resolved open questions -> **Archive/audio-design-history.md**
  (ARCHIVED); the grain-banks north star + its incremental growth path (in-loop-out / parameter
  system / blend cues / composite cues) -> **Backlog/audio-followups.md**.

## P1 - batch 7: input (2026-08-12)

Verified against code @ 9e80f286.

- **Systems/input.md** REWRITTEN present-tense (was `Draconic Input - Action Mapping (design)`,
  SHIPPED-but-design-framed, 210 lines, old `draconic.input.*`/`draconic.shell` names). Now a CURRENT
  reference: the raw device layer (foundation.shell) vs the action layer (foundation.input), the
  modules (foundation.input.resource / input.pipeline / engine.input / editor.input), the InputMap
  data model, ActionRuntime evaluation + interactions, UI-vs-game arbitration, play-in-editor,
  resources + rebind overlay, editor + scripting, touch + portability. Verified against code:
  foundation.shell (InputEvent + device facades + InputSurface/InputRouter); foundation.input
  ActionRuntime.cppm + InputMap.cppm + ConsumptionMask(SetConsumptionMask) + interactions
  Hold/Tap/DoubleTap; InputMapResource (foundation.input.resource); InputMapAsset (input.pipeline);
  InputSubsystem + IInputSourceProvider (engine.input); InputMapPage + Listen capture (editor.input);
  Input facade (per-context, Wren); InputBindingOverrides settings overlay. State: unknown -> current.
- CORRECTED drift vs the design/header: the ConsumptionMask UI-vs-game arbitration is SHIPPED and
  WIRED (Engine.UI/UISubsystemImpl.cpp calls SetConsumptionMask) - the old status header still listed
  it as "REMAINING: awaits the game-ui subsystem". ActionRuntime lives in foundation.input (not a
  separate engine eval lib). PlayerInput is genuinely absent (deferred).
- SPLIT (spec rule): the reference survey (Sedulous/ez/Godot/Flax) + design calls + resolved open
  questions -> **Archive/input-design-history.md** (ARCHIVED); the remaining P3 (PlayerInput pairing,
  action haptics, per-scene/split-screen) -> **Backlog/input-followups.md**.
