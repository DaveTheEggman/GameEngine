// SPDX-License-Identifier: MIT
// Copyright (c) 2026-Present Robert Campbell

// Editor::Scene - the Game tab's resolution choices (Sedulous c5100e94): the project's first,
// then the export presets that draw at their own size, then the user's preview resolutions,
// then the panel's own size; a saved key finds its entry, and a gone one falls back to the
// project's.
#include <doctest/doctest.h>
#include "Core/Prelude.h"

import foundation.core;
import engine.project;
import editor.core;
import editor.scene;

using namespace foundation::core;
using namespace editor;

TEST_CASE("game-resolution: the project's first, then the presets, the previews, the panel")
{
    // Nothing at all: the project's entry (the panel's size) and the panel's.
    {
        const Array<GameResolutionChoice> bare = CollectGameResolutions(nullptr, nullptr, nullptr);
        REQUIRE(bare.Size() == 2u);
        CHECK(bare[0].key == kProjectResolutionKey);
        CHECK(bare[0].label == StringView(u8"Project (panel size)"));
        CHECK(bare[0].width == 0u);
        CHECK(bare[1].key == kPanelResolutionKey);
    }

    engine::project::ProjectSettings settings;
    settings.renderWidth = 640;
    settings.renderHeight = 360;
    ExportPresetSet presets;
    ExportPreset desktop; // draws at the project's size: not offered
    desktop.name = String(u8"Desktop");
    ExportPreset handheld;
    handheld.name = String(u8"Handheld");
    handheld.overridesRender = true;
    handheld.renderWidth = 1280;
    handheld.renderHeight = 800;
    presets.presets.PushBack(desktop);
    presets.presets.PushBack(handheld);
    GamePreviewSettings previews;
    previews.presets.PushBack(GamePreviewResolution{String(u8"QHD"), 2560, 1440});
    previews.presets.PushBack(GamePreviewResolution{String(u8"Broken"), 0, 1440}); // skipped

    const Array<GameResolutionChoice> choices = CollectGameResolutions(&settings, &presets, &previews);
    REQUIRE(choices.Size() == 4u);
    CHECK(choices[0].label == StringView(u8"Project 640x360"));
    CHECK(choices[0].width == 640u);
    CHECK(choices[1].key == StringView(u8"export:Handheld"));
    CHECK(choices[1].label == StringView(u8"Handheld 1280x800"));
    CHECK(choices[2].key == StringView(u8"preset:QHD"));
    CHECK(choices[2].height == 1440u);
    CHECK(choices[3].key == kPanelResolutionKey);
    CHECK(choices[3].width == 0u);

    const Span<const GameResolutionChoice> view(choices.Data(), choices.Size());
    CHECK(FindGameResolution(view, u8"preset:QHD") == 2u);
    CHECK(FindGameResolution(view, u8"preset:Gone") == 0u); // the project's comes back

    // The signature follows the sizes and the fit, so a change rebuilds the dropdown.
    const String before = GameResolutionSignature(view, FitMode::Letterbox);
    CHECK(GameResolutionSignature(view, FitMode::Letterbox) == before);
    CHECK(GameResolutionSignature(view, FitMode::Crop) != before);
}
