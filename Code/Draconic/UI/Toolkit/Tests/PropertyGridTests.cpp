// Smoke test for the toolkit PropertyGrid: add editors, query by name/count, remove and clear.
#include <doctest/doctest.h>
#include "Core/Prelude.h"
import draconic.core;
import draconic.ui;
import draconic.ui.toolkit;

using namespace draconic::ui;
using namespace draconic::ui::toolkit;
using namespace draconic::core;
namespace core = draconic::core;

TEST_CASE("toolkit-propertygrid: AddQueryRemoveClear")
{
    auto grid = core::MakeRef<PropertyGrid>(core::DefaultAllocator());
    // Constructed with a ScrollView child.
    CHECK(grid->ChildCount() == 1u);
    CHECK(grid->PropertyCount() == 0u);

    grid->AddProperty(core::MakeRef<BoolEditor>(core::DefaultAllocator(), StringView(u8"Visible"), true));
    grid->AddProperty(core::MakeRef<FloatEditor>(core::DefaultAllocator(), StringView(u8"Mass"), 1.0));
    grid->AddProperty(core::MakeRef<IntEditor>(core::DefaultAllocator(), StringView(u8"Layer"), 0));
    CHECK(grid->PropertyCount() == 3u);

    PropertyEditor* mass = grid->GetProperty(StringView(u8"Mass"));
    REQUIRE(mass != nullptr);
    CHECK(mass->Name() == StringView(u8"Mass"));
    CHECK(grid->GetProperty(StringView(u8"Nope")) == nullptr);

    grid->RemoveProperty(StringView(u8"Layer"));
    CHECK(grid->PropertyCount() == 2u);
    CHECK(grid->GetProperty(StringView(u8"Layer")) == nullptr);

    grid->Clear();
    CHECK(grid->PropertyCount() == 0u);
}

TEST_CASE("toolkit-propertygrid: CategoriesAndDisplayName")
{
    auto grid = core::MakeRef<PropertyGrid>(core::DefaultAllocator());
    auto ed = core::MakeRef<BoolEditor>(core::DefaultAllocator(), StringView(u8"CastsShadows"), false,
        Function<void(bool)>{}, StringView(u8"Rendering"));
    ed->SetDisplayName(StringView(u8"Casts Shadows"));
    CHECK(ed->DisplayName() == StringView(u8"Casts Shadows"));
    CHECK(ed->Category() == StringView(u8"Rendering"));
    grid->AddProperty(Move(ed));
    CHECK(grid->PropertyCount() == 1u);
    CHECK(grid->PropertyAt(0)->Category() == StringView(u8"Rendering"));
}
