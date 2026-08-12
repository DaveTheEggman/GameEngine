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

## P1 - batch 8: game-ui (2026-08-12)

Verified against code @ 3364be29.

- **Systems/game-ui.md** REWRITTEN present-tense (was `Draconic Game UI - draconic.ui as a runtime
  subsystem (design)`, 400 lines: SHIPPED-but-design-framed with a huge inline changelog + the
  reference survey + the Sedulous deep-read tier table + locked decisions + a live backlog, old
  `draconic.ui.*` names). Now a CURRENT reference: the framework vs the new modules (foundation.ui.
  resource / ui.pipeline / engine.ui / editor.gameui), the three tiers (screen UICanvasComponent /
  billboards UIBillboardComponent / world UIWorldPanelComponent), overlay roles + rendering + split-
  screen + RT canvas mode, input (consumption mask + per-surface scene binding + gamepad/text),
  resources + the UI facade, locked decisions, deferred. Verified against code: modules above;
  UISubsystem + UICanvasComponent (CanvasRenderMode) + UIBillboardComponent + UIWorldPanelComponent +
  SpriteOrientation::EntityOriented + ISceneOverlay/IScreenOverlay + ConsumptionMask + GameTheme +
  defaultUiThemeId (engine.ui); UIDocument/UITheme (foundation.ui.resource); UIDocumentAsset/UIThemeAsset
  (ui.pipeline); UIDocumentPage (editor.gameui); the id-addressed UI facade (setText/setProgress/
  setVisible + onClick delegate). State: unknown -> current.
- CORRECTED drift: the world tier SHIPPED (UIWorldPanelComponent - the old doc had it DEFERRED/OUT of
  phasing) and script wiring SHIPPED as id-addressed control access (locked-decision 6 had it PARKED on
  entity handles).
- SPLIT (spec rule): the reference survey (Sedulous/Flax/Godot/Traktor Spark) + the Sedulous.Engine.UI
  deep-read tier table + the locked-decision rationale + the world-tier A-vs-B decision ->
  **Archive/game-ui-design-history.md** (ARCHIVED); the remaining follow-ups (option-A direct-draw,
  dirty-gating, atlas/MIP, declarative bindings, theme variations, per-line diagnostics, the two-scene
  edge, the toolkit test tail) -> **Backlog/game-ui-followups.md**.

## P1 - batch 9: game-instance (2026-08-12)

Verified against code @ 33b89288.

- **Systems/game-instance.md** REWRITTEN present-tense (was `Draconic - GameInstance (design)`, 470
  lines: Phase-1-SHIPPED but overwhelmingly a design + phasing + investigation LOG - the retrofit
  framing (§1-10), the target model (§11), the run-host-ownership investigation (§11.10), and the
  SceneSubsystem-pure-registry step (§11.11), all inline). The whole track shipped, so it is now a
  CURRENT reference: the two layers (engine host owns Array<GameInstance>; GameInstance owns its
  ScriptRunHost + SceneManager + per-instance NetworkManager + ActionRuntime + headless + time scale),
  SceneManager (scene lib, ticks its group, ISceneAware fan-out via the registry), SceneSubsystem as a
  pure registry, the time model, per-instance run-host binding, the SceneLoader scene-management facade,
  standalone/editor/headless, deferred. Verified against code: engine.gameinstance (GameInstance.cppm)
  owning m_runHost/m_sceneManager/m_net(NetworkManager)/m_inputRuntime(ActionRuntime)/m_headless;
  SceneManager (scene lib); ScriptSubsystem keeps only m_ownedRunHost + ConfigureRunHost +
  MaybeTeardownRunHost (per-host); SceneSubsystem AwareRegistry + RegisterManager + ForEachManager;
  DefaultApplication PrimaryScenes(); SceneLoaderScriptBinding loadScene/loadSceneAsync/loadComplete/
  sceneReady + starter snippets in all three cooks. State: unknown -> current.
- CORRECTED drift: the whole §11 target model SHIPPED (old header said "Phase 1 SHIPPED, Phases 2-5
  designed") - per-instance run hosts, SceneManager, pure-registry SceneSubsystem, the SceneLoader
  facade, per-instance net + input, headless are all present in code.
- SPLIT (spec rule): the retrofit diagnosis (the duplicated bracket, the ownership audit, the blockers,
  the two framings, the run-host-ownership investigation, the Zero GameSession precedent, the per-page
  scene-manager decision) -> **Archive/game-instance-design-history.md** (ARCHIVED); the two open items
  (player multi-scene compositing, the one-shared-editor-manager alternative) ->
  **Backlog/game-instance-followups.md**.

## P1 - correction sweep: deferred/backlog claims re-verified (2026-08-12)

Prompted by the user catching a FALSE deferral. The batch method verified SHIPPED claims against code
but had trusted the old docs' DEFERRED items without re-checking - and those are the items most likely
to have been quietly completed. Re-verified every deferred/backlog claim from batches 4-9 against code:

- **FALSE DEFERRAL (fixed): game-ui "UIDocumentPage code-editor control".** The page
  (`Editor.GameUI/UIDocumentPage.cppm`) ALREADY uses `ui::toolkit::CodeEditView` (gutter, monospace,
  virtualized, XML lexer, `<`-triggered markup completion, native undo) + a debounced live preview. I
  had lifted a STALE "multi-line EditText / honest v1" comment from the `.cpp` verbatim. Corrected in
  Systems/game-ui.md + Backlog/game-ui-followups.md to the real remaining sliver (per-line gutter
  diagnostics; parse errors currently show as inline status over the preview).
- **UNDER-CLAIM (fixed): networking facade.** Systems/networking.md listed only isServer/isClient/
  peerCount/startServer/connect but the `Net` facade also registers SEND-side RPC (rpc/rpcNumber/
  rpcText). Added them; noted the RECEIVE side (Net.on) is the genuinely-unbuilt P3 slice 1.
- **CONFIRMED still deferred (checked against code):** networking receive-RPC-into-script (no `on`
  binding), networking web WebSocket client (comment-only in NetTransport/NetModule, no emscripten
  websocket backend), networking Net.spawn (test-hash only, no facade); physics convex decomposition
  (cook kinds ConvexHull/TriangleMesh only), gravity volumes, JPH_DEBUG_RENDERER, per-world job pool;
  audio grain banks + in-loop-out + setParameter (SoundCue comment only); input PlayerInput (absent) +
  haptics (no rumble/Haptic in code); game-ui declarative markup bindings (only a material-binding
  comment); game-instance player multi-scene compositing. These stand.

Method fix going forward: verify DEFERRED items against code too, not just shipped claims.

## P1 - correction sweep 2: batches 1-3 deferred claims re-verified (2026-08-12)

Per the user, re-verified batches 1-3 (scripting, script-debugger, asset-pipeline) deferred/backlog
claims against code with the corrected method.

- **FALSE DEFERRAL (fixed): scripting "ScriptClassesView - API browser / autocomplete - UI unbuilt".**
  It IS built: `ScriptApiBrowserView` + `ScriptApiSurface` (one bound-API source for browser +
  completion, off `DescribeBoundApi()`) + `ScriptCompletionImpl`, wired into `ScriptPage`
  (own impl files, added to the layout, click-to-insert). Corrected Backlog/scripting-followups.md
  (struck through, marked DONE) and enriched Systems/scripting.md's ScriptPage line.
- **CONFIRMED still deferred (checked against code):**
  - scripting: profiler (`IScriptProfiler` seam exists but `ScriptDebug.cppm` explicitly states "no
    backend implements it yet, CreateProfiler returns null"); luau-analyze (LuauScript.cppm comments
    only, no subprocess); Entity-in-facades (physics.subsystem STILL `import foundation.script.facades`
    for the `rayHitEntity` return type - the contract-lib move is not done); behaviors live-state
    reload (fields are transient, no getter convention); behaviors script-defined editor hooks (absent);
    AngelScript delegate richer signatures (one general funcdef accepting any signature - the
    per-signature split is genuinely additive); Wren retirement (user-deferred).
  - script-debugger: remote transport (no RemoteScriptDebugger/DebugServer), Wren debugger, step-out /
    conditional breakpoints / watch expressions - none in the backends.
  - asset-pipeline: GPU thumbnails (all `thumb` refs in AssetsView/AssetPickerSlot/Project are
    explicitly "future/reserved/not yet drawn"; no render-to-thumb cache) and cooked per-target
    platform variants (absent). Both hold.

Tally across both correction sweeps: 2 false-deferrals fixed (game-ui code-editor, scripting API
browser), 1 under-claim fixed (net send-RPC); everything else verified still-deferred against code.

## P1 - batch 10: runtime-host (2026-08-12)

Verified against code @ 42273d13.

- **Systems/runtime-host.md** REWRITTEN present-tense (was `Runtime Host Hardening - Design (v2)`, 533
  lines: IMPLEMENTED but a layered design log - v1 module-list, v2 inversion, v3 embedded host, all
  with full API sketches + a Sedulous wart table + multi-window frame-flow pseudocode, old
  `raptor.runtime.*`/`Raptor/Runtime/` names). Now a CURRENT reference: the inversion (IApplication IS
  the app; DefaultApplication; no IApplicationModule), ApplicationHost + uniform multi-window + the
  three graphics modules, subsystem phases + per-window render, the embedded host (two-context editor),
  time + run ownership pointing to game-instance. Verified against code: foundation.runtime.client
  (Application.cppm IApplication hooks Settings/Configure/OnStartup/OnLaunch/OnUpdate/OnFixedUpdate/
  OnRenderWindow/OnExit/OnShutdown; ApplicationHost.cppm loop-agnostic generic host + uniform
  multi-window; EmbeddedHost.cppm routes Ctx to embedded runtime context + shares real GraphicsDevice +
  MainRenderWindow null); foundation.graphics{,.gpu,.null} (GraphicsDevice/RenderWindow/FrameContext);
  foundation.runtime.desktop RunApplication; engine.defaultapp DefaultApplication. State: unknown ->
  current.
- CORRECTED drift (both-shipped-and-deferred method): IApplicationModule is DEAD (only a stale comment
  in Graphics.cppm) - the inversion replaced it. The proposed `Subsystem::Render(FrameContext&)` phase
  was NEVER added (Subsystem phases are BeginFrame/FixedUpdate/Update/PostUpdate/EndFrame); per-window
  render goes through IApplication::OnRenderWindow. The doc's per-scene-time framing is superseded by
  the SceneManager model (game-instance).
- SPLIT (spec rule): the v1/v2/v3 evolution + the Sedulous wart table + the API sketches + the deferred-
  then-obsoleted Subsystem::Render item + deviations -> **Archive/runtime-host-design-history.md**
  (ARCHIVED). No live backlog: the deferred design was superseded, not left pending.

## P1 - batch 11: code-editor (2026-08-12)

Verified against code @ b97b3952 (both-shipped-and-deferred method).

- **Systems/code-editor.md** REWRITTEN present-tense (was `Code editor - CodeEditView (ui.toolkit)`,
  TRACK COMPLETE but a phase-history + decisions log, old `draconic.*`/`Draconic::` names). Now a CURRENT
  reference: the widget (CodeEditView/CodeDocument, buffer/render/editing/markers), lexing + languages,
  completion, diagnostics + debugger seam, consumers, deferred. Verified against code (foundation.ui.
  toolkit): CodeEditView, CodeDocument, ICodeLexer, CLikeLexer, XmlLexer, LuaLikeLexer, CodeHighlighter,
  CodeLexerRegistry, ICompletionProvider, MarkupCompletionProvider; per-language editor targets
  editor.script{,.wren,.angelscript,.luau}; ScriptApiCompletionProvider + EditorContext ScriptExecution
  Point + ScriptValueProbe. State: unknown -> current.
- UPDATED since the doc was written: LuaLikeLexer + editor.script.luau now exist (the doc predated the
  Luau backend) - added Luau to the lexers + consumers.
- RESIDUALS re-verified still-deferred vs code: HLSL lexer (no HlslLexer), regex search (none in the
  find bar), structured markup-warning line info (none), IME preedit, folder naming. All hold.
- SPLIT (spec rule): the decisions (purpose-built widget, completion-from-start, layering) + the
  P1-P4 phase history -> **Archive/code-editor-design-history.md** (ARCHIVED); the residuals ->
  **Backlog/code-editor-followups.md**.

## P1 - batch 12: prefabs (2026-08-12)

Verified against code @ 84e710a7 (both-shipped-and-deferred method).

- **Systems/prefabs.md** REWRITTEN present-tense (was `Prefabs - survey + design`, status "surveyed +
  design proposed, NOT started (2026-07-14)" - badly STALE; the track SHIPPED P1-P4 + user-confirmed).
  Now a CURRENT reference: asset + payload (PrefabDocument, scene-format), spawn + per-scene instance
  tracking, derived (not tracked) overrides, nesting + wire, editor + propagation. Verified against
  code (foundation.scene / foundation.scene.resource): PrefabDocument, ScenePrefabMode,
  PrefabInstanceState (tracked by root, componentBaselines + baselineTransforms), SpawnPrefab /
  SpawnPrefabInstance, PrefabComponentBaseline / PrefabMemberInfo, nested-instance records + prefabId +
  prefabProvider resolver, kPrefabWireReferenced3 / kPrefabWireExpanded2; Editor.Scene/ModelPrefab.cppm
  (model->prefab). State: unknown -> current.
- CORRECTED drift: the header said NOT started; the design's "nesting deferred (P4)" actually SHIPPED
  (nested records + resolver + Referenced-v3 wire); the "overrides derived, not tracked" design call
  shipped as PrefabComponentBaseline.
- SPLIT (spec rule): the Sedulous prefab-V2 survey + the design proposal + phasing + open questions ->
  **Archive/prefabs-design-history.md** (ARCHIVED). No live backlog - the track completed.

## P1 - batch 13: text-scenes (2026-08-12)

Verified against code @ 84e710a7 (both-shipped-and-deferred method).

- **Systems/text-scenes.md** REWRITTEN present-tense (was `Text Scenes - XML source streams...`,
  status APPROVED 2026-07-17, SHIPPED with the two follow-ups marked DONE inline, old `draconic.*` +
  `DraconicExport` names). Accurate technical content preserved, names refreshed, present-tense. Verified
  against code: foundation.xml.serialization + foundation.scene.resource; SceneStreamEncoding{Binary,
  Text} + DetectSceneStreamEncoding (sniffs `<`); one SerializeScene path; WriteComponentRecord inline
  (text param) via WriteComponent; export staging in Editor.Core/Export + Tools.Export with
  AddAllSceneManagers. State: unknown -> current.
- No Archive/Backlog split needed (a focused decision doc, no survey; the two follow-ups were already
  DONE, folded into the shipped text; the one optional pretty-formatting item kept as an inline Deferred
  line).

## P1 - batch 14: path-type (2026-08-12)

Verified against code @ 89516efc (both-shipped-and-deferred method).

- **Systems/path-type.md** REWRITTEN present-tense (was `A typed Path for source references`, status
  PLANNED / not implemented - STALE; P1+P2 SHIPPED). Now a CURRENT reference: SourcePath (foundation.vfs),
  adoption (Asset::fileName), non-goals, deferred. Verified against code: foundation.vfs/SourcePath.cppm;
  Pipeline.Core/Asset.cppm (`Asset::fileName is vfs::SourcePath`) inherited by all asset types
  (confirmed across font/texture/audio/script/image editor pages + tests); Editor.App/PathPickerDialog.
  State: unknown -> current.
- CORRECTED drift + verified deferred: P1 (type) + P2 (root adoption) SHIPPED; P3 (GENERIC inspector
  auto-picker) is NOT built - the reflected inspector has no SourcePath dispatch; editor pages hand-wire
  PathPickerDialog (confirmed: no SourcePath in InspectorView/AssetForm/Reflect). P4 (Check-Assets lint)
  NOT built (no CheckAssets action). Both kept as Deferred.
- SPLIT (spec rule): the Traktor traktor::Path survey + sharp-edges-not-copied + design calls ->
  **Archive/path-type-design-history.md** (ARCHIVED); P3/P4 kept as an inline Deferred section.

## P1 - batch 15: settings + project-and-settings (2026-08-12)

Verified against code @ 1abdb98d (both-shipped-and-deferred method).

- **Systems/settings.md** REWRITTEN present-tense (was `Draconic - Settings system (design)`, status
  "design 2026-07-15" - SHIPPED). Now a CURRENT reference for the foundation.settings primitive. Verified
  against code: foundation.settings module (Settings.cppm) - Settings::Section<T>/MarkChanged<T>/OnChanged
  + Load/Save(IStream, SerializerFactory, TypeRegistry); unknown-section passthrough SHIPPED
  (m_unknownSections + UnknownSectionCount + verbatim re-emit + binary-abort); user-data-dir helper in
  foundation.core/System.cppm. State: unknown -> current.
- CORRECTED drift: the designed single-store Default/User/Project LAYER STACK did NOT ship (no
  SettingsLayer enum, no LoadLayer/SaveLayer); layering is separate store INSTANCES per file. The dynamic
  PropertyBag + SettingsKey<T> (P3) was NOT built (deferred). Module home is foundation.settings, not
  draconic.core.
- **Systems/project-and-settings.md** REFRESHED (was already a good 2026-08-01 present-tense reference):
  names draconic.* -> engine.project / editor.core / editor.app / foundation.settings; ProjectSettings
  data version v6 -> v8 (verified RTTI_DEFINE_OBJECT_VERSIONED(ProjectSettings,...,8)); Draconic.Tools.
  Editor -> Tools.Editor; added the status header + the settings.md cross-link. Verified: engine.project
  (ProjectSettings), ProjectManagerController, RecentProjectsSettings (editor.core/ProjectRegistry),
  ProjectManagerView, EditorExportSettings, EditorDockLayoutSettings. State: unknown -> current.
- SPLIT (spec rule): the Traktor survey + the original single-store-layer-stack design (superseded) +
  the phased plan -> **Archive/settings-design-history.md** (ARCHIVED). Dynamic-bag deferral kept inline.

## P1 - batch 16: export cluster (export + export-templates + export-reachability) (2026-08-12)

Verified against code @ b5d0418b (both-shipped-and-deferred method).

- **Systems/export.md** REWRITTEN present-tense (was `Draconic - Export & Export Templates (design)`,
  "mostly SHIPPED" with a "QUEUED - handed to fable" section + historical §4/§10/§11). The two QUEUED
  items + the "remaining" UI ALL SHIPPED, so now a CURRENT reference. Verified: editor.core
  (:export_pipeline/:export_preset/:export_template/:export_roots); ExportPreset/ExportTemplate/
  TemplateRegistry/ExportContent/ExportProject/ExportResult/ResolveTemplatesRoot; CreateTemplate + CLI
  Tools.Export --template create; ExportPreset::pruneToReachable (v3); Manage-Templates + OpenPresetEditor
  UI. State: unknown -> current.
- **Systems/export-templates.md** REWRITTEN present-tense (was "design, extends the shipped system").
  The config axis SHIPPED: ExportTemplate.config/compiler, EffectiveConfig(), FindBy(platform,config),
  host-<platform>-<config>, CreateTemplate stamps config; categorized sidecars SHIPPED (sidecars=required
  + symbols=opt-in); Web template synthesis. Deferred verified: baseline engine content (shaders baked
  in today), capabilities field, downloadable templates. State: unknown -> current.
- **Systems/export-reachability.md** REFRESHED present-tense (was "Phases 1+2 SHIPPED" - accurate).
  Verified: pruneToReachable + ExportRoots (:export_roots) + CollectExportRoots + SceneRefScanner
  main-thread pre-scan + export-report. Deferred verified: Phase 3 AssetRef<T> (no AssetRef type), Phase
  4 script-load lint (absent, stretch goal), Phase 5 runtime capture, minor UI follow-ups. State:
  unknown -> current.
- SPLIT (spec rule): the problem framing + the templates-vs-presets rationale + the QUEUED-then-shipped
  history + precursors + config-driven-sidecars ruling -> **Archive/export-design-history.md** (ARCHIVED).

## P1 - batch 17: editor-jobs + viewport-input (2026-08-12)

Verified against code @ 7e4ebae7 (both-shipped-and-deferred method).

- **Systems/editor-jobs.md** REFRESHED present-tense (was accurate but old path). Path
  Code/Draconic/Editor/Draconic.Editor.Core -> Code/Editor/Editor.Core/JobService.cppm (module
  editor.core); FontEditorPage path -> Code/Editor/Editor.Fonts; added header. Verified: EditorJobService
  + Submit (build lane, folds into cook MutationLock) + SubmitLight (light lane, own worker, outside
  IsBusy). State: unknown -> current.
- **Systems/viewport-input.md** REWRITTEN present-tense (was `DESIGN (2026-07-03) - not implemented` -
  STALE; SHIPPED). Now a CURRENT reference: the unified problem, the layer (per-window tagging + focus,
  event-first + poll snapshot, InputSurface gated facades, InputRouter), one-fit-function/one-enum.
  Verified against code: foundation.shell InputSurface.cppm (InputSurface = ContentFit slice; Mouse()/
  Keyboard() gated facades; ContentMouse() normalized [0,1]; SetFit/SetFitMode; HoverWindow), InputTypes
  (InputEvent stream), single FitMode enum; foundation.ui.viewport ViewportView consumes InputRouter.
  State: unknown -> current.
- SPLIT (spec rule): the Sedulous reference read (the gem kept + the smells not ported + the four locked
  decisions) -> **Archive/viewport-input-design-history.md** (ARCHIVED). editor-jobs needed no split.

## P1 - correction: export baseline-content (shaders) was stale (2026-08-12)

User caught a stale claim in batch 16's export-templates.md: "baseline engine content ~none - core
render-pass shaders are inline HLSL baked into the player binary, so passes need no external shader
pak." OUTDATED - I carried the old doc's open-question bullet without verifying against the
shaders-out-of-C++ track, which SHIPPED. Reality (verified): engine shaders are .hlsl source cooked by
foundation.shaders/ShaderPackCooker; export stages a shaders.dpak beside the player for the target
platform (StageShaderPack, ExportImpl.cpp:104/705 - Vulkan/DXIL desktop, WGSL web) so every dist renders
with no external dependency. Fixed export-templates.md (baseline content is SHIPPED, export-produced per
platform) + export.md (noted the shaders.dpak staging in the uniform driver). Same failure mode as the
game-ui code-editor / scripting API-browser false-deferrals: a carried claim not re-checked. Extra
caution flagged for the shaders.md / shaders-materials-hot-reload.md batch.

## P1 - batch 18: instanced-mesh + skinning-benchmark (2026-08-12)

Verified against code @ 7e4ebae7.

- **Systems/instanced-mesh.md** - status stamp + name fix (title dropped "- Design"; draconic.animation.
  subsystem -> engine.animation; SS for section refs to avoid the section glyph). The body was accurate
  IMPLEMENTED reference. Verified: InstancedMeshComponent (engine.render/RenderComponents), InstancedSkinning
  (engine.animation), DataOffsets (foundation.render/MeshRenderer), samples RenderStressTest/AnimatedCrowd
  exist. §10 open-questions-resolved deferrals point to Plans/renderer-improvements.md §2. State: unknown
  -> current.
- **Systems/skinning-benchmark.md** - status stamp added. A benchmark reference with measured before/after
  numbers (AnimStressTest sample verified to exist; kAutoRamp measurement aid). The one open limitation
  (#3: full skeleton spawned as ~34 entities/char) remains the noted CPU frontier. State: unknown ->
  current. Light touch (accurate present-tense reference; no rewrite needed).
