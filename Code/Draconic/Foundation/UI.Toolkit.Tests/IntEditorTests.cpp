// Smoke test for the toolkit IntEditor: value round-trip + NumericField change drives the setter.
#include <doctest/doctest.h>
#include "Core/Prelude.h"
import draconic.core;
import draconic.ui;
import draconic.ui.toolkit;

using namespace draconic::ui;
using namespace draconic::ui::toolkit;
using namespace draconic::core;
namespace core = draconic::core;

TEST_CASE("toolkit-inteditor: RoundTripAndFieldChange")
{
    i64 observed = 0;
    auto ed = core::MakeRef<IntEditor>(core::DefaultAllocator(), StringView(u8"Count"), 5, 0, 100,
                                       Function<void(i64)>{[&observed](i64 v) { observed = v; }});

    CHECK(ed->Value() == 5);

    auto* field = core::Cast<NumericField>(ed->EditorView());
    REQUIRE(field != nullptr);
    CHECK(field->DecimalPlaces() == 0);

    field->SetValue(42.0);
    CHECK(ed->Value() == 42);
    CHECK(observed == 42);

    ed->SetValue(7);
    CHECK(field->Value() == doctest::Approx(7.0));
}
