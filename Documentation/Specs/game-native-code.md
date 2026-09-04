# Game native code

Status: SPEC (2026-09-04). Track branch: `shared-libraries` (builds directly on
that track; see shared-libraries.md sections 6-7 for the identity/rendezvous
foundation and the full Traktor audit this plan is grounded in).

Goal: a project can carry native C++ game code. In DEV it is a plugin the
shared-build editor/player loads (fast iteration, play-in-editor). For SHIPPING
the exporter links it statically into a branded player executable (full LTO,
single artifact, works on every platform incl. web). Same source both ways.

## The model (decided)

- Game native code is a MODULE like any engine library: declared through
  util_add_engine_library, following every house rule (tests, allocator
  idiom, tripwires). One artifact kind - the Traktor lesson.
- The game exposes an IRuntimePlugin (the existing Foundation::Runtime
  contract): OnLoad(Context&) performs ALL registration explicitly
  (components, script facades, subsystems). No static-init registration -
  this is what makes dual-mode linking trivial (no forced-reference chains,
  no whole-archive; Traktor's Module.cpp tax does not exist here).
- DEV (ENGINE_SHARED_LIBS builds): PluginHost::Load dlopens the game module
  (the ruled plugin model, proven by Runtime.CrossPlugin). SHIP: a generated
  stub passes CreateGamePlugin() into the launcher; PluginHost::Add - the
  static path that already exists - registers it on the same Context at the
  same point. The two modes converge on PluginHost.
- The SEAM (verified in code): the runtime Context is owned by the
  application host (IApplicationHost::Ctx()); PlayerApplication extends
  DefaultApplication; Main.cpp's header already names this exact evolution:
  "projects that outgrow scripts graduate to a native IApplication at the
  same seam".

## Phases

- N1 - launcher-as-library. Engine.Player's main() moves into a
  Engine.Player.Main STATIC library exposing
  PlayerMain(argc, argv, IRuntimePlugin* nativeGame /*nullable*/); the
  Engine.Player executable becomes a thin stub passing nullptr. Zero
  behavior change; verified in static + shared lanes. (The Traktor trick:
  Runtime.App builds as an executable in dev and a main()-carrying static
  library for the ship link.)
- N2 - dev loop (player half SHIPPED 2026-09-04). The manifest field
  ALREADY EXISTED (ProjectSettings.nativeModule, reserved + serialized -
  no version bump). Convention: a PROJECT-RELATIVE path to the built
  module (e.g. Native/libMyGame.so). PlayerApplication::OnStartup hosts a
  PluginHost after the base startup (engine subsystems resolvable in
  OnLoad) and before initial-scene resolution (plugin types deserialize):
  ship path = PluginHost::Add(options.nativeGame); dev path =
  PluginHost::Load(projectDir/nativeModule), failure reported and the run
  continues (scripts still work). OnShutdown unloads the game FIRST, while
  Ctx() lives. Proven end to end: shared player + a scratch project
  declaring Runtime.CrossPlugin -> "native game module ... loaded
  ('CrossPlugin')". N2 COMPLETE: the editor half loads the
  module against the embedded runtime context at project open (toast +
  console on failure), and the Project Settings dialog carries the
  free-text "Native module" row.
- N3 - ship link (mechanism SHIPPED 2026-09-04; exporter integration
  remaining). SIMPLIFICATION over the original plan: nothing is generated.
  The engine build HOSTS the game - ENGINE_GAME_NATIVE_DIR (cache var) adds
  the game's native directory as a subdirectory after all engine targets
  (full module/BMI access; util_add_engine_library gives the dev .so and
  the ship static lib from one source), and ENGINE_GAME_NATIVE_TARGET
  materializes Engine.GamePlayer: a CHECKED-IN ShipMain.cpp (extern "C"
  CreatePlugin - the same symbol PluginHost dlopens in dev - statically
  referenced, so the game archive's registration links with no
  whole-archive/forced-ref machinery) + Engine.Player.Main + the game
  target + runtime-dep staging + $ORIGIN. PROVEN: a scratch game module
  wired by hand builds Engine.GamePlayer with ZERO engine .so deps, and
  its plugin OnLoad runs through PluginHost::Add, taking precedence over
  the manifest's dlopen module. EXPORTER HALF SHIPPED
  2026-09-04: ExportOne detects nativeModule (non-web presets), invokes the
  BAKED cmake (BUILDSYSTEM_CMAKE_PATH) over the BAKED engine root with the
  BAKED compiler (the system default silently differed - pinned to the
  editor's own toolchain), builds Engine.GamePlayer in a persistent
  per-project dir (<project>/.cache/ship-<config>; incremental relinks;
  build type = preset.config), with a per-project Bin suffix
  (-Ship-<name>), and stages it as the dist player in place of the
  template binary. Convention: native source at <project>/Native; target
  name derived from the nativeModule basename. PROVEN end to end via
  Tools.Export on the scratch project: the dist's ScratchGame binary boots
  dist mode and runs the statically-linked plugin. Speed note: the
  target-scoped build compiles only the player's dependency closure (~a
  quarter of the tree). Tests: target-derivation + suffix helpers.
  Remaining: a committed fixture project for CI-able E2E; editor-side
  export UI needs no change (same ExportOne).
- N4 - authoring. New-project wizard/template for a native game module
  (CMakeLists via util_add_engine_library, plugin skeleton, facade
  registration example + tests). Docs.
- N5 - platforms. Windows (waits on the MSVC modules/dllexport prototype
  for the DEV loop; the SHIP path is static and needs no export macros);
  web (SHIP-only by design - the game module compiles into the wasm player
  in the export wasm build; no dev-loop dlopen on web).

- N6 - hot reload (DESIGNED, not scheduled; the gnarly part). What the
  plugin left behind decides everything - the inventory on unload:
  subsystems in Context (OnUnload removes - existing contract), TypeInfo*
  in GlobalTypeRegistry pointing INTO the .so, factory/function pointers in
  SerializableRegistry + script facade registries + resource factories,
  live RefCounted objects whose vtables + destroy fns live in the .so,
  script bindings + live script objects over plugin types, scene components
  from plugin managers.

  RULING - RUN-BRACKETED RELOAD ONLY: reload happens between runs, never
  during one. Editor flow: stop the PIE run (scenes + script contexts are
  transient and die with it - plugin-created objects go with them) ->
  scope-reversed unregistration -> dlclose -> dlopen the rebuilt module ->
  OnLoad -> restart the run (scene state reloads from disk). NO live-state
  migration/reinstancing - out of scope BY DESIGN (that is the Unreal
  reinstancing swamp; run-transient PIE makes it unnecessary).

  EDITING SCENES (the MyFancyComponent case - a plugin component authored
  in a scene that is OPEN FOR EDITING when the reload happens): the run
  bracket extends to a SCENE BRACKET riding the existing Simulate-snapshot
  machinery. (1) SNAPSHOT every open scene to memory through the wire
  format (unsaved edits included - the wire format is the contract, not
  the memory layout); (2) DESTROY the scenes while the OLD .so is still
  loaded (component + manager teardown runs against the code that created
  them); (3) scope-reverse, liveness-guard (now also: no scene alive holds
  module types), dlclose, dlopen, OnLoad; (4) RESTORE from snapshots -
  components resolve by name/id through the NEW module's registrations,
  managers are recreated by its scene-manager contribution, inspectors
  rebuild via TypeId. Serialization IS the migration layer: a layout
  change goes through the normal DataVersion gates; an ungated change
  fails that payload LOUDLY (the strict-versioning rule working as
  intended during native iteration - a signal, never corruption). Raw
  pointers into component storage across the reload are already illegal
  (resolve-per-use rule; pools swap-remove even without reloads).

  MECHANISM - RegistrationScope (SHIPPED 2026-09-04 for the type +
  serializable registries): PluginHost arms AMBIENT registration observers
  for the duration of OnLoad - the plugin registers through the normal
  global calls, everything actually INSERTED is recorded per entry, and
  UnloadAll reverses the recording (reverse order) after OnUnload, before
  dlclose. Observers fire only on real inserts, so a type another party
  already owns is never recorded or torn down. TypeRegistry +
  SerializableRegistry gained Unregister + SetRegistrationObserver
  (remove-by-id - the P1 groundwork). PROVEN in the shared lane: the
  cross-boundary case now runs the full round-trip (load -> registered ->
  unload -> GONE from the registry -> reload -> re-registered + OnLoad
  resolves the host subsystem again). EDITOR RELOAD FLOW SHIPPED
  (v1): Project > Reload Native Module - stops any game run
  (StopGameRun), scope-reversed UnloadAll(closeLibraries=false) so the OLD
  mapping stays alive for the process lifetime (leak-on-purpose:
  DynamicLibrary::Detach - never free pages under a pointer teardown
  missed), then loads a FRESH VERSIONED COPY from .cache/native-hot/
  (dlopen refcounts by path; only a new file yields a new module), OnLoad
  re-registers; toasts the outcome. The scene bracket is deliberately
  ABSENT in v1: plugin scene-manager contributions do not exist yet, so no
  plugin component data can live in editing scenes - the snapshot/restore
  bracket lands WITH that seam. Still open: script-facade registries (die
  with per-run script contexts; audit when dynamic rebinding lands).

  SAFETY - liveness guard before dlclose: verify the run is stopped and the
  module's types have no live instances; on ANY doubt, SKIP dlclose (leak
  the old .so deliberately - unreachable stale code is harmless, a freed
  page under a live vtable is not) and still load + register the new
  module (the scope removed the old registry entries, so the new ones
  take by id).

  CACHE RULE (extends the P1 identity rule): long-lived caches store
  TypeId and re-resolve TypeInfo* per use via FindById/Canonical - a
  cached TypeInfo* does not survive a reload.

## Rules established

- Native game code never registers via static initializers; OnLoad is the
  one registration entry.
- The ship path never invokes a linker directly; it generates CMake.
- Dev-loop native loading exists only in shared builds; a static
  editor/player with a project declaring a native module reports it
  clearly and runs without it (scripts still work).
