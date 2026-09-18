// SPDX-License-Identifier: MIT
// Copyright (c) 2026-Present Robert Campbell

// glTF loader: node transforms. A glTF node carries EITHER a TRS triple or a 4x4 matrix
// (column-major, column-vector convention). Every consumer of a ModelBone reads its TRS
// fields (the cook copies them into the resource's Transform), so both spellings must land
// there - a matrix-only node used to leave the fields at identity and cook at the origin.
#include <doctest/doctest.h>
#include "Core/Prelude.h"

#include <cstdio>
#include <cstring>
#include <string>

import foundation.core;
import foundation.model;
import foundation.model.gltf;

using namespace foundation::core;
using namespace foundation::model;

namespace
{
    // Two nodes describing the SAME transform: translation (10,0,0), uniform scale 2, no
    // rotation. glTF's matrix is column-major, so the translation sits in the last four.
    constexpr const char* kDocument = R"({
  "asset": {"version": "2.0"},
  "scene": 0,
  "scenes": [{"nodes": [0, 1]}],
  "nodes": [
    {"name": "viaMatrix", "matrix": [2,0,0,0, 0,2,0,0, 0,0,2,0, 10,0,0,1]},
    {"name": "viaTrs", "translation": [10,0,0], "scale": [2,2,2]}
  ]
})";

    const ModelBone* FindBone(const Model& model, StringView name)
    {
        for (const ModelBone* bone : model.bones())
        {
            if (bone->name() == name)
            {
                return bone;
            }
        }
        return nullptr;
    }
}

TEST_CASE("gltf: a matrix-only node lands in the TRS fields and agrees with the TRS node")
{
    const char* path = "scratch_gltf_matrix_node.gltf";
    // The engine's own file write (std::fopen is a C4996 error under MSVC /WX).
    REQUIRE(WriteFile(StringView(reinterpret_cast<const utf8char*>(path)),
                      Span<const byte>(reinterpret_cast<const byte*>(kDocument),
                                       std::strlen(kDocument)))
                .IsOk());

    Model model;
    gltf::GltfLoader loader;
    REQUIRE(loader.load(StringView(reinterpret_cast<const utf8char*>(path)), model) ==
            ModelLoadResult::Ok);
    REQUIRE(model.bones().Size() == 2u);

    const ModelBone* viaMatrix = FindBone(model, u8"viaMatrix");
    const ModelBone* viaTrs = FindBone(model, u8"viaTrs");
    REQUIRE(viaMatrix != nullptr);
    REQUIRE(viaTrs != nullptr);

    // The matrix node's TRS fields are populated (this is what the cook reads)...
    CHECK(viaMatrix->translation.x == doctest::Approx(10.0f));
    CHECK(viaMatrix->translation.y == doctest::Approx(0.0f));
    CHECK(viaMatrix->translation.z == doctest::Approx(0.0f));
    CHECK(viaMatrix->scale.x == doctest::Approx(2.0f));
    CHECK(viaMatrix->scale.y == doctest::Approx(2.0f));
    CHECK(viaMatrix->scale.z == doctest::Approx(2.0f));
    CHECK(viaMatrix->rotation.w == doctest::Approx(1.0f));

    // ...and both spellings produce the same local matrix, in the engine's convention.
    for (int r = 0; r < 4; ++r)
    {
        for (int c = 0; c < 4; ++c)
        {
            CHECK(viaMatrix->localTransform.m[r][c] ==
                  doctest::Approx(viaTrs->localTransform.m[r][c]));
        }
    }
    const Float3 p = TransformPoint(Float3{1.0f, 0.0f, 0.0f}, viaMatrix->localTransform);
    CHECK(p.x == doctest::Approx(12.0f));

    std::remove(path);
}
