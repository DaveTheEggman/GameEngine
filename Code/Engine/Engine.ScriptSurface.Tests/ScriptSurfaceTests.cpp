// Engine.ScriptSurface.Tests - the drift + idempotency tripwire for the script-surface root.
//
// RegisterAllScriptFacades is the single source of truth for the complete subsystem facade set.
// These tests assert it installs exactly the expected number of extra facade names (a new
// subsystem facade must bump kSubsystemFacadeNameCount deliberately; a lost one fails loudly) and
// that it is idempotent against repeated calls - the property a host relies on when it registers
// the surface once at startup.

#include <doctest/doctest.h>

#include "Core/Prelude.h"

import foundation.core;
import foundation.script.facades;
import engine.scriptsurface;

using namespace foundation::core;

TEST_CASE("engine.scriptsurface: RegisterAllScriptFacades installs exactly the subsystem facades")
{
    engine::RegisterAllScriptFacades();
    const usize count = foundation::script::ExtraFacadeNames().Size();
    CHECK(count == engine::kSubsystemFacadeNameCount);
}

TEST_CASE("engine.scriptsurface: RegisterAllScriptFacades is idempotent (registries dedup)")
{
    engine::RegisterAllScriptFacades();
    const usize first = foundation::script::ExtraFacadeNames().Size();
    engine::RegisterAllScriptFacades();
    const usize second = foundation::script::ExtraFacadeNames().Size();
    CHECK(second == first);
}
