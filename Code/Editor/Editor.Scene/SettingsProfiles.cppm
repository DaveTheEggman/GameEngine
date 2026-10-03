// SPDX-License-Identifier: MIT
// Copyright (c) 2026-Present Robert Campbell
// Editor::Scene - :settings_profiles partition.
//
// A scene settings block whose values can come from a shared profile asset (the render
// profiles): making a profile from a block's values, and writing a profile's values back to its
// asset. Nothing here names a block or a profile type: the block answers its profile's product
// (SceneSystem::SettingsProfileType), the editor's join of factories and builders answers the
// asset type that makes it, and the asset takes the values through pipeline::SettingsProfileAsset.
module;
#include "Core/Prelude.h"

export module editor.scene:settings_profiles;

import foundation.core;
import foundation.content;
import foundation.scene;
import pipeline.core;
import editor.core;
import :edit;

using namespace foundation::core;

export namespace editor
{
    /// The creator that makes `system`'s profile asset, or null (no profile, or no creator).
    [[nodiscard]] const pipeline::AssetCreator*
    SettingsProfileCreator(EditorContext& context, foundation::scene::SceneSystem& system);

    /// Writes a settings block's `values` (the profile asset's values layout) into the profile
    /// asset `instance`, its other fields kept.
    Status WriteSettingsProfileValues(foundation::content::Instance& instance, const void* values);

    /// An edit made live in `profile` (the values `system` has in effect): the asset's write is
    /// queued for the save flow (EditorContext::RegisterAssetEdit), the values taken now.
    void QueueSettingsProfileEdit(EditorContext& context, foundation::scene::SceneSystem& system,
                                  const Guid& profile);

    /// Make profile: a new profile asset (named `name`, in the creator's default group) holding
    /// the block's values in effect, and the block switched to it (one undo step). The new
    /// instance, or null with a notice.
    foundation::content::Instance* MakeSettingsProfile(EditorContext& context,
                                                       SceneEditContext& edit,
                                                       const TypeInfo* settingsType,
                                                       StringView name);
}
