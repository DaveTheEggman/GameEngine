// Engine::ScriptSurface - the `engine.scriptsurface` module.
//
// The SCRIPT-SURFACE composition root: the Engine-side sibling of Pipeline::Registration (MCP spec
// Fable ruling 2026-08-09). A surface-DESCRIBING host - the headless MCP server, a future API-
// browser export - needs the COMPLETE bound scripting surface: every subsystem's script facade
// (RigidBody, Audio, Input, Ui, ...), not just the core Entity/Log/Time/Random. Each subsystem
// already exposes a metadata-ONLY registrar (RegisterPhysicsScriptFacade et al. - they touch only
// the reflection/facade registries, never a device/world/GPU), so the surface is assembled by
// CALLING them, never by instantiating a subsystem. This root links the subsystem libs (headless-
// linkable: RHI.Null, audio Null mode) and calls all of them behind one entry point.
//
// EVERY host calls this root - surface-describing hosts (MCP, cook, script_api) AND the runtime
// (DefaultApplication, which instantiates the full subsystem set anyway). The runtime originally
// kept a hand-rolled per-subsystem list; it drifted past RegisterRunScriptFacade and exported
// games could not resolve `run` (week-2026-08-22). One list, one tripwire.
//
// The wide subsystem imports live in the implementation unit, keeping this interface BMI lean for
// the hosts that consume it (GCC module-interface hygiene).

module;
#include "Core/Prelude.h"

export module engine.scriptsurface;

import foundation.core;

using namespace foundation::core;

export namespace engine
{
    /// Register the COMPLETE engine script surface into the global registries: core types + the
    /// base behavior facades (Entity/Log/Time/Random) + every subsystem facade (physics, audio,
    /// input, UI, render, particles, animation, scene-loader, net). Metadata only - no subsystem is
    /// instantiated, no device is created; safe in a fully headless host. Idempotent (the registries
    /// dedup), so a host may call it once at startup and every surface query sees the full set.
    void RegisterAllScriptFacades();

    /// Tripwire count (Engine.ScriptSurface.Tests asserts against this): the number of EXTRA facade
    /// names RegisterAllScriptFacades installs beyond the base behavior facades - i.e. the subsystem
    /// facades. A new subsystem facade bumps this deliberately; a lost registration fails loudly.
    // 33 = +RayCastHit (2026-08-22, script-surface-of P0: ScenePhysics.rayCast returns the
    // explicit hit-result value handle; the stored lastHit + hit* accessors are deleted).
    // 34 = +OverlapHits (2026-08-26: ScenePhysics.overlapSphere returns the full overlap set as an
    // indexed count()/entity(i) handle - the facade surface has no native array<T> to return).
    inline constexpr usize kSubsystemFacadeNameCount = 34;
}
