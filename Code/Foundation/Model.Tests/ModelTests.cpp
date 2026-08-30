// SPDX-License-Identifier: MIT
// Copyright (c) 2026-Present Robert Campbell

#include <doctest/doctest.h>
#include "Core/Prelude.h"

import foundation.core;
import foundation.model;

using namespace foundation::core;
using namespace foundation::model;

TEST_CASE("model: core data types - names round-trip as wide strings")
{
    ModelMaterial mat;
    mat.setName(u8"steel");
    CHECK(mat.name() == u8"steel");

    Model model;
    CHECK(model.meshes().Size() == 0u);
}
