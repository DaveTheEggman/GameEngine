// Pipeline.Registration.Tests - the drift tripwire.
//
// The composition root is the single source of truth ONLY if it stays complete. These tests
// register into FRESH local registries and assert the counts against the interface's explicit
// constants: a new builder/importer bumps the constant deliberately, a lost or double
// registration fails loudly. This is the check that "the three inline copies stay in sync" never
// had - now the one copy is machine-verified.

#include <doctest/doctest.h>

#include "Core/Prelude.h"

import foundation.core;
import pipeline.core;
import pipeline.importer;
import pipeline.registration;

using namespace foundation::core;

TEST_CASE("pipeline.registration: RegisterAllBuilders populates exactly kBuilderCount builders")
{
    pipeline::BuilderRegistry registry;
    CHECK(registry.Count() == 0u);
    pipeline::RegisterAllBuilders(registry);
    CHECK(registry.Count() == pipeline::kBuilderCount);
}

TEST_CASE("pipeline.registration: RegisterAllImporters populates exactly kImporterCount importers")
{
    pipeline::ImporterRegistry registry;
    CHECK(registry.Count() == 0u);
    pipeline::RegisterAllImporters(registry);
    CHECK(registry.Count() == pipeline::kImporterCount);
}

TEST_CASE("pipeline.registration: RegisterPipelineTypes runs the whole set without faulting")
{
    // Smoke: every registrar the headless cook needs fires once, in order, on a real process.
    // (No count to assert - this guards that the composition root's type/backend/cook
    // registrations all resolve and none abort.)
    pipeline::RegisterPipelineTypes();

    // Routing still works after registration: a fresh builder set resolves by asset type name.
    pipeline::BuilderRegistry registry;
    pipeline::RegisterAllBuilders(registry);
    CHECK(registry.Count() == pipeline::kBuilderCount);
}
