// Pipeline::Registration - the `pipeline.registration` module.
//
// The pipeline's COMPOSITION ROOT as a library. Every host that cooks or imports - the CLI
// cooker, the export packager, the editor, the headless MCP server - needs the SAME set of
// builders, importers, and product/resource type registrations. Registering that set inline in
// each host's main() duplicates it verbatim; a builder added to two of three copies is a silent
// gap (cook works in the editor, missing from CLI export - the collision-cook class of bug).
// This library is the single source of truth: it links every pipeline module (its entire job -
// the deliberate fan-in point) and exposes three entry points the hosts call instead of
// restating the list.
//
// The wide imports live in the implementation unit, not here: this interface stays lean (three
// declarations + the tripwire counts) so the four hosts that consume it do not each pull the
// whole pipeline into their BMI (GCC module-interface hygiene).

module;
#include "Core/Prelude.h"

export module pipeline.registration;

import foundation.core;
import pipeline.core;
import pipeline.importer;

using namespace foundation::core;

export namespace pipeline
{
    /// Register every asset/product/resource type, script backend, and per-language cook the
    /// pipeline ships, into the global type/serializable/cook registries. A headless cook needs
    /// these because ReadObject constructs cooked products BY TYPE NAME. Call once at startup,
    /// before planning a cook. Does NOT touch a BuilderRegistry (that is RegisterAllBuilders) and
    /// registers NO editor-UI services (those are the editor's own concern, kept host-side).
    void RegisterPipelineTypes();

    /// Populate `registry` with every asset builder the engine ships. Independent of
    /// RegisterPipelineTypes (constructs builder instances; needs no prior type registration),
    /// but a cook needs both. Idempotent per registry (a fresh registry each call).
    void RegisterAllBuilders(BuilderRegistry& registry);

    /// Populate `registry` with every OS-file importer the engine ships (the drag-drop / MCP
    /// asset_import surface): texture, model, UI, audio, script, font.
    void RegisterAllImporters(ImporterRegistry& registry);

    // === Tripwire counts (Pipeline.Registration.Tests asserts against these) ===
    // A new builder/importer bumps the matching constant DELIBERATELY; a lost registration then
    // fails the test loudly. This converts "nobody checks the three copies stay in sync" into
    // "the build checks the one copy is complete".
    inline constexpr usize kBuilderCount = 26;
    inline constexpr usize kImporterCount = 9;
}
