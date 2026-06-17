#include <doctest/doctest.h>
#include "Core/Prelude.h"

import raptor.core;
import raptor.model;

using namespace raptor::core;
using namespace raptor::model;

TEST_CASE("model: core data types — names round-trip as wide strings")
{
    ModelMaterial mat;
    mat.setName(u"steel");
    CHECK(mat.name() == u"steel");

    Model model;
    CHECK(model.meshes().Size() == 0u);
}
