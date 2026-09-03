// SPDX-License-Identifier: MIT
// Copyright (c) 2026-Present Robert Campbell

// HeightfieldEditorPage tests (headless): factory type-dispatch + the asset blob round-trip the
// page's undo snapshots ride. The preview + grid need a live harness (editor app).

#include <doctest/doctest.h>

#include "Core/Prelude.h"

import foundation.core;
import heightfield.pipeline;
import foundation.vfs;
import editor.heightfield;
import editor.core;

using namespace foundation::core;

TEST_CASE("HeightfieldEditorPageFactory reports the HeightfieldAsset primary type")
{
    editor::HeightfieldEditorPageFactory factory;
    CHECK(factory.PrimaryType() == &pipeline::HeightfieldAsset::StaticType());
}

TEST_CASE("HeightfieldEditor registers a factory the registry routes for HeightfieldAsset")
{
    editor::EditorContext context{DefaultAllocator()};
    editor::RegisterHeightfieldEditor(context);

    editor::IEditorPageFactory* found =
        context.Pages().FindFactory(pipeline::HeightfieldAsset::StaticType());
    REQUIRE(found != nullptr);
    CHECK(found->PrimaryType() == &pipeline::HeightfieldAsset::StaticType());
}

TEST_CASE("HeightfieldAsset blob snapshot round-trips its authored fields (undo path)")
{
    pipeline::HeightfieldAsset a;
    a.fileName = foundation::vfs::SourcePath(u8"Terrain/island.png");
    a.size = 513;
    a.worldSize = Float2{512.0f, 512.0f};
    a.minY = -8.0f;
    a.maxY = 96.0f;

    MemoryStream stream;
    {
        BinarySerializer ar(stream, SerializeMode::Write);
        a.Serialize(ar);
        REQUIRE(ar.IsOk());
    }
    (void)stream.Seek(0, SeekOrigin::Begin);
    pipeline::HeightfieldAsset b;
    {
        BinarySerializer ar(stream, SerializeMode::Read);
        b.Serialize(ar);
        REQUIRE(ar.IsOk());
    }
    CHECK(b.fileName.View() == u8"Terrain/island.png");
    CHECK(b.size == 513);
    CHECK(b.worldSize.x == doctest::Approx(512.0f));
    CHECK(b.minY == doctest::Approx(-8.0f));
    CHECK(b.maxY == doctest::Approx(96.0f));
}
