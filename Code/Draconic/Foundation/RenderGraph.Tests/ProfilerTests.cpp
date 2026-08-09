// GPU-free guard/bounds coverage for GraphProfiler (Sedulous ships no profiler
// test; the timing path needs a real device + fence wait, exercised in samples).
#include <doctest/doctest.h>

#include "Core/Prelude.h"

import draconic.core;
import draconic.rendergraph;

using namespace foundation::core;
using namespace foundation::rendergraph;

TEST_CASE("rg.profiler: uninitialized is safe and reports zero")
{
    GraphProfiler profiler;
    CHECK(profiler.enabled);

    // No Init() called: queries are zero, out-of-range indices are clamped.
    CHECK(profiler.GetPassTimeMs(0) == 0.0f);
    CHECK(profiler.GetPassTimeMs(-1) == 0.0f);
    CHECK(profiler.GetPassTimeMs(1000) == 0.0f);

    profiler.SetTimestampPeriod(1.0f); // no-op without GPU state
    profiler.Destroy();                // safe with no device
    CHECK(profiler.GetPassTimeMs(0) == 0.0f);
}
