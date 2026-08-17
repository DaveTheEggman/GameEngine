// Engine::SceneSurface - the `engine.scenesurface` module.
//
// The SCENE-SURFACE composition root: the serialization sibling of Engine::ScriptSurface. A
// headless scene consumer - the CLI export's transcode scratch, the MCP server's scene_validate,
// any future tool that must LOAD a scene without a running engine - needs every serializable
// component manager and settings-bearing scene system present, or the scene reader silently skips
// their records ("skipping records of unknown component type"). Each engine domain exports its
// own Add<Domain>SceneManagers(Scene&) which its subsystem's OnSceneCreated ALSO delegates to, so
// the runtime injection and this headless aggregate can never drift: a manager added to the domain
// function reaches both automatically. This root just calls all eight domain functions (plus the
// matching component-reflection registrars) behind one entry point.
//
// Managers are plain value pools and the settings systems construct inert (no device, no engine,
// no run host), so the aggregate is safe in a fully headless process. The RUNTIME keeps
// per-subsystem injection - a game's manager set is exactly what its subsystems create; this is
// the deliberate superset for consumers that must understand ANY scene stream.
//
// The wide subsystem imports live in the implementation unit, keeping this interface BMI lean
// (GCC module-interface hygiene).

module;
#include "Core/Prelude.h"

export module engine.scenesurface;

import foundation.core;
import foundation.scene;

using namespace foundation::core;

export namespace engine
{
    /// Add EVERY serializable component manager + settings-bearing scene system to `scene` (the
    /// union of all subsystems' OnSceneCreated injections, via the same per-domain functions the
    /// subsystems call). For headless scratch scenes that deserialize arbitrary scene streams -
    /// export transcode, MCP validation. All systems construct inert; nothing here touches a
    /// device, an audio engine, or a script host.
    void AddAllSceneManagers(foundation::scene::Scene& scene);

    /// Register every domain's component reflection (data-version gates + field metadata) - must
    /// run once before any scene stream with component payloads deserializes. Idempotent.
    void RegisterAllSceneComponentReflection();

    /// Tripwire count (Engine.SceneSurface.Tests asserts against this): the TOTAL number of scene
    /// systems AddAllSceneManagers installs, across all domains. Adding a manager to any
    /// Add<Domain>SceneManagers bumps this deliberately; a lost registration fails loudly.
    inline constexpr usize kSceneSystemCount = 31;
}
