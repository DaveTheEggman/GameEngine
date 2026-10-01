// SPDX-License-Identifier: MIT
// Copyright (c) 2026-Present Robert Campbell

// Editor::Scene - :game_resolution partition (Sedulous c5100e94).
//
// The resolutions the Game tab offers to draw at. The project's own render resolution comes
// first and is the default; then each export preset that draws at its own size, as that
// platform would; then the user's preview resolutions (GamePreviewSettings, Preferences); then
// the panel's own size. A fixed choice is fitted into the panel by the project's render fit, as
// the player fits it into its window. Kept apart from the page so the list is testable without
// a Game tab.

module;
#include "Core/Prelude.h"

export module editor.scene:game_resolution;

import foundation.core;
import engine.project;
import editor.core;

using namespace foundation::core;

export namespace editor
{
    inline constexpr StringView kProjectResolutionKey = u8"project";
    inline constexpr StringView kPanelResolutionKey = u8"panel";

    /// One entry of the Game tab's resolution dropdown.
    struct GameResolutionChoice
    {
        String key;   // "project", "export:<preset>", "preset:<name>" or "panel"
        String label; // what the dropdown shows
        u32 width = 0; // nought for the panel's own size
        u32 height = 0;
    };

    /// The choices, in the dropdown's order. Any source may be absent (no project, no preset
    /// file, no user store); the project's entry and the panel's are always there.
    [[nodiscard]] inline Array<GameResolutionChoice> CollectGameResolutions(
        const engine::project::ProjectSettings* settings, const ExportPresetSet* presets,
        const GamePreviewSettings* previews)
    {
        Array<GameResolutionChoice> out;
        {
            GameResolutionChoice project;
            project.key = String(kProjectResolutionKey);
            if (settings != nullptr && settings->HasRenderResolution())
            {
                project.width = settings->renderWidth;
                project.height = settings->renderHeight;
                project.label = Format(u8"Project {}x{}", project.width, project.height);
            }
            else
            {
                project.label = String(u8"Project (panel size)");
            }
            out.PushBack(Move(project));
        }
        if (presets != nullptr)
        {
            for (const ExportPreset& preset : presets->presets)
            {
                if (!preset.overridesRender || preset.renderWidth == 0 || preset.renderHeight == 0)
                {
                    continue;
                }
                out.PushBack(GameResolutionChoice{
                    Format(u8"export:{}", preset.name.AsView()),
                    Format(u8"{} {}x{}", preset.name.AsView(), preset.renderWidth, preset.renderHeight),
                    preset.renderWidth, preset.renderHeight});
            }
        }
        if (previews != nullptr)
        {
            for (const GamePreviewResolution& preview : previews->presets)
            {
                if (preview.width == 0 || preview.height == 0)
                {
                    continue;
                }
                out.PushBack(GameResolutionChoice{
                    Format(u8"preset:{}", preview.name.AsView()),
                    Format(u8"{} {}x{}", preview.name.AsView(), preview.width, preview.height), preview.width,
                    preview.height});
            }
        }
        out.PushBack(GameResolutionChoice{String(kPanelResolutionKey), String(u8"Fit to panel"), 0, 0});
        return out;
    }

    /// The entry a saved key names; the project's (the first) when it names none any more.
    [[nodiscard]] inline usize FindGameResolution(Span<const GameResolutionChoice> choices, StringView key)
    {
        for (usize i = 0; i < choices.Size(); ++i)
        {
            if (choices[i].key.AsView() == key)
            {
                return i;
            }
        }
        return 0;
    }

    /// What the choices were built from, and the fit they apply with: the dropdown rebuilds only
    /// when this changes.
    [[nodiscard]] inline String GameResolutionSignature(Span<const GameResolutionChoice> choices, FitMode fit)
    {
        String signature;
        for (const GameResolutionChoice& choice : choices)
        {
            signature += Format(u8"{}={}x{};", choice.key.AsView(), choice.width, choice.height).AsView();
        }
        signature += Format(u8"fit={}", static_cast<i32>(fit)).AsView();
        return signature;
    }
}
