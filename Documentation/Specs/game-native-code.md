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
  ('CrossPlugin')". REMAINING N2 TAIL: editor play-in-editor loads the
  module at its embedded host seam; a project-settings editor row for the
  field; a toast (console-only today).
- N3 - ship link. Tools.Export gains the native step: GENERATE A SMALL
  CMAKE PROJECT (never raw linker driving - the Traktor anti-lesson):
  a stub cpp (calls PlayerMain(CreateGamePlugin())) + the game module
  sources/lib + the engine built as static libs from the engine checkout
  (v1 SDK answer; BMIs are not shippable artifacts, so the game compiles
  against engine module sources with local BMIs, cached). Invoke
  cmake --build via the export pipeline; stage into the dist exactly like
  the current prebuilt-player templates.
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

  MECHANISM - RegistrationScope: OnLoad registers through a recording scope
  owned by PluginHost (type ids, factory keys, facade names); unload
  REVERSES the recording automatically instead of trusting hand-written
  OnUnload symmetry. Prereq: Unregister APIs on TypeRegistry /
  SerializableRegistry / script registries - cheap and safe now that maps
  key on TypeId (remove by id; the P1 groundwork pays off again).

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
