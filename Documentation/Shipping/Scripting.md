# Scripting

How gameplay code works in a game project. This is the workflow guide; the AUTHORITATIVE
API surface (every bound type, method, and facade, per backend) comes from the MCP
`script_api` tool - always prefer it over memorized signatures.

## Backends

Three script languages are supported: **Wren**, **AngelScript**, and **Luau**. A project can
mix them - each script asset declares its language. The bound engine surface is the same
across backends; syntax and idioms differ (e.g. Luau uses colon calls for instance methods,
dot calls for static facades).

## The three script tiers

- **Behavior** - attached to an entity through a ScriptComponent behavior slot. One class per
  script; the engine constructs it with the entity handle. This is where most gameplay lives.
- **Level** - the per-scene script (reserved class name `Level`), set in the scene's Scene
  Script settings. One instance per scene, constructed with the scene handle. Use it for
  scene-wide orchestration: spawning, win conditions, sequencing.
- **Game** - the project's startup script (reserved class name `Game`), set in project
  settings. Runs for the whole game run, across scene loads.

## Lifecycle and events

Handlers dispatch **by presence** - implement only what you need:

- `onStart()` - after the entity/scene is live.
- `onUpdate(dt)` - per frame, `dt` in seconds. (Levels also get `onFixedUpdate`.)
- `onDestroy()` - before teardown.
- `on<Event>(...)` - named events: physics contacts, and any custom event another script
  sends. `entity.send("eventName", payload)` delivers to the target entity's behaviors.

## Editor properties

Fields initialized in a behavior's constructor become editor-visible properties (numbers,
booleans, strings, and vectors are harvested). Keep constructors to plain field
initialization - they also run during cooking.

## Working through the MCP tools

- `script_api` - the live bound API for a chosen backend. Read it before writing code.
- Scripts are project assets: import/list/cook them like any asset (see Assets.md).
- Coroutines are available in every backend for multi-frame sequences.

## Gotchas

- Class names `Game` and `Level` are reserved for their tiers - do not use them for
  behaviors.
- A behavior constructor runs at cook time too; side effects beyond field init will
  misbehave.
