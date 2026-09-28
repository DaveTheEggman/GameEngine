// SPDX-License-Identifier: MIT
// Copyright (c) 2026-Present Robert Campbell

// Engine::Composition - the `engine.composition` module.
//
// THE composition root: the one list of engine domains (Documentation/Specs/engine-composition.md,
// D5), each declared once in its own library (engine.domain's DomainModule), and the facets a
// consumer asks the list for - the scene composition every scene assembles from, the component
// reflection, the resource types, the factories a host can create, the script facades. The
// runtime (DefaultApplication), the editor, the headless tools (export, cook, the MCP hosts)
// and the samples compose from this root and from nothing else, so no executable can drift
// from the domains it links. It replaces Engine.Composition (the scene list) and
// Engine.Composition (the facade list), whose entry points keep their names below.
//
// The wide domain imports live in the implementation unit, keeping this interface BMI lean
// (GCC module-interface hygiene).

module;
#include "Core/Prelude.h"

export module engine.composition;

import foundation.core;
import foundation.scene;
import foundation.resource;
export import engine.domain;

using namespace foundation::core;

export namespace engine
{
    /// The full engine composition: every domain, in dependency order. Built once.
    [[nodiscard]] const EngineComposition& FullComposition();

    /// The scene facet: every serializable component manager and settings-bearing scene system
    /// across all domains. `Instantiate(scratch)` installs the full manager set;
    /// `RegisterReflection()` registers every scene component's reflection.
    [[nodiscard]] const foundation::scene::SceneComposition& FullSceneComposition();

    /// Adds EVERY serializable component manager and settings system to `scene`: the headless
    /// scratch scenes (export transcode, MCP validation, the scene format reference) and the
    /// runtime assemble from the same list.
    void AddAllSceneManagers(foundation::scene::Scene& scene);

    /// Registers every domain's component reflection (data-version gates + field metadata); must
    /// run once before any scene stream with component payloads deserializes. Idempotent.
    void RegisterAllSceneComponentReflection();

    /// Registers every resource module's types: the cooked records and products factories
    /// construct by type name. Idempotent.
    void RegisterAllResourceTypes();

    /// Registers the COMPLETE engine script surface: core types, the base behaviour facades
    /// (Entity/Log/Time/Random) and every domain's facade. Metadata only - no subsystem is
    /// instantiated, no device is created; safe in a fully headless host. Idempotent.
    void RegisterAllScriptFacades();

    /// Tripwire count (Engine.Composition.Tests asserts against this): the number of EXTRA facade
    /// names RegisterAllScriptFacades installs beyond the base behaviour facades - the domain
    /// facades. A new domain facade bumps this deliberately; a lost registration fails loudly.
    // 33 = +RayCastHit: ScenePhysics.rayCast returns the explicit hit-result value handle.
    // 34 = +DebugDraw: DebugDraw.of(scene) immediate-mode debug draw facade.
    inline constexpr usize kSubsystemFacadeNameCount = 36; // + SplineHit + SceneSplines
}
