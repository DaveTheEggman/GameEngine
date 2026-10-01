// SPDX-License-Identifier: MIT
// Copyright (c) 2026-Present Robert Campbell

// Engine::Project - ProjectSettings' reflection: the settings the Project Settings dialog edits,
// described once so the dialog and the MCP tools read the same list from the type. Kept out of
// the interface (a REFLECT_MEMBERS body there makes GCC emit a gcm cluster).

module;
#include "Core/Prelude.h"
#include "Core/Reflection/Reflect.h"

module engine.project;

import foundation.core;

using namespace foundation::core;

namespace engine::project
{
    REFLECT_MEMBERS(ProjectSettings, "rtti::engine::project")
    {
        builder.DataVersion(9);
        const auto asset = [&builder](StringView label, StringView type, StringView emptyText)
        {
            builder.PropAttribute(kSettingLabelAttribute, String(label))
                .PropAttribute(kSettingAssetTypeAttribute, String(type))
                .PropAttribute(kSettingEmptyTextAttribute, String(emptyText));
        };
        builder.Property<&ProjectSettings::name>("name")
            .PropAttribute(kSettingLabelAttribute, String(u8"Name"));
        // A project-relative path to the built native game module; empty = scripts only.
        builder.Property<&ProjectSettings::nativeModule>("nativeModule")
            .PropAttribute(kSettingLabelAttribute, String(u8"Native module"));
        // The scene the player and play-in-editor open.
        builder.Property<&ProjectSettings::defaultSceneId>("defaultSceneId");
        asset(u8"Default scene", u8"SceneDocument", u8"(none)");
        // The cooked ScriptClass the player (and the Game tab) binds at startup.
        builder.Property<&ProjectSettings::startupScriptId>("startupScriptId");
        asset(u8"Startup script", u8"ScriptClassAsset", u8"(none)");
        // The cooked map the player (and the Game tab) binds at startup.
        builder.Property<&ProjectSettings::defaultInputMapId>("defaultInputMapId");
        asset(u8"Default input map", u8"InputMapAsset", u8"(none)");
        // The cooked mixer applied at startup; nil = the built-in neutral four-bus layout.
        builder.Property<&ProjectSettings::defaultBusLayoutId>("defaultBusLayoutId");
        asset(u8"Default bus layout", u8"AudioBusLayoutAsset", u8"(built-in)");
        // The cooked UITheme the game UI defaults to; nil = the built-in GameTheme.
        builder.Property<&ProjectSettings::defaultUiThemeId>("defaultUiThemeId");
        asset(u8"Default UI theme", u8"UIThemeAsset", u8"(built-in)");
        // The cooked UIDocument shown as the boot splash while the default scene streams.
        builder.Property<&ProjectSettings::loadingDocumentId>("loadingDocumentId");
        asset(u8"Loading screen", u8"UIDocumentAsset", u8"(built-in)");
        // The cooked font the game UI falls back to when a document names none.
        builder.Property<&ProjectSettings::defaultUiFontId>("defaultUiFontId");
        asset(u8"Default UI font", u8"FontAsset", u8"(built-in)");
        // Scene-pass MSAA samples (the render subsystem's levels: 1 = off, 2, 4).
        builder.Property<&ProjectSettings::renderMsaaSamples>("renderMsaaSamples")
            .PropAttribute(kSettingLabelAttribute, String(u8"MSAA"));
        // Cooked fonts the game UI loads beside the default one, each a family a label picks.
        RegisterArrayType<Guid>(); // the list container (the dialog's list, the MCP tools)
        builder.Property<&ProjectSettings::uiFontIds>("uiFontIds");
        asset(u8"Other UI fonts", u8"FontAsset", u8"(none)");
    }
}
