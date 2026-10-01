// SPDX-License-Identifier: MIT
// Copyright (c) 2026-Present Robert Campbell

// Editor::App - :settings_dialog partition.
//
// ProjectSettingsDialog: a modal editor for the project manifest (Project.xml) - the fields a
// user meaningfully changes from inside the editor: project name, the default scene (picked
// through the guid-authoritative AssetPickerDialog, filtered to scenes), and the startup game
// script path. The engine version stamp is shown read-only (every save re-stamps it to the
// running engine; the launcher/project-manager owns migration).
//
// [Save] writes the fields back into EditorProject::Settings() and persists the manifest;
// [Cancel]/Escape discards. Every asset setting is a ResourceRefEditor row: pick, drop and clear
// set the instance GUID (rename/move-proof); Save keeps the path alongside as a mirror.

module;
#include "Core/Prelude.h"
#include "Core/Log/Log.h"
#include "Core/Reflection/Reflect.h"

export module editor.app:settings_dialog;

import foundation.core;
import foundation.content;
import foundation.ui;
import engine.render; // the canonical MSAA level table (kMsaaLevels + index<->samples helpers)
import editor.core;
import :resource_ref_editor;

using namespace foundation::core;

export namespace editor::app
{
    namespace ui = foundation::ui;
    namespace content = foundation::content;

    class ProjectSettingsDialog final : public ui::Dialog
    {
        RTTI_OBJECT(ProjectSettingsDialog, ui::Dialog)
    public:
        explicit ProjectSettingsDialog(editor::EditorContext& context)
            : ui::Dialog(u8"Project Settings"), m_context(&context)
        {
            MinWidth.SetValue(460.0f);
            MinHeight.SetValue(240.0f);
            MaxWidth.SetValue(560.0f);
            MaxHeight.SetValue(560.0f); // taller so the ~8 rows fit; the ScrollView handles overflow

            editor::EditorProject* project = context.Project();

            auto column = MakeRef<ui::FlexLayout>(MemoryAllocator());
            column->Direction = ui::Orientation::Vertical;
            column->Spacing = 8;

            m_nameEdit = AddTextRow(*column, u8"Name",
                                    project != nullptr ? project->Settings().name.AsView()
                                                       : StringView(u8""));

            // Native game module (game-native-code.md N2): a project-relative path to the
            // built module (e.g. Native/libMyGame.so); empty = scripts only. Free text -
            // the module is built outside the editor, so there is nothing to pick from.
            m_nativeModuleEdit =
                AddTextRow(*column, u8"Native module",
                           project != nullptr ? project->Settings().nativeModule.AsView()
                                              : StringView(u8""));

            // The asset settings: each a slot that picks, takes a dropped asset of its type and
            // clears, seeded from the manifest (editor-lists-and-asset-slots.md P1).
            const auto* settings =
                project != nullptr ? &project->Settings() : nullptr;
            // The default scene the player and play-in-editor open.
            AddAssetRow(*column, u8"Default scene", m_sceneId, u8"SceneDocument", u8"(none)",
                        settings != nullptr ? settings->defaultSceneId : Guid{});
            // The cooked ScriptClass the player (and the Game tab) binds at startup.
            AddAssetRow(*column, u8"Startup script", m_scriptId, u8"ScriptClassAsset", u8"(none)",
                        settings != nullptr ? settings->startupScriptId : Guid{});
            // The cooked map the player (and the Game tab) binds at startup.
            AddAssetRow(*column, u8"Default input map", m_inputMapId, u8"InputMapAsset",
                        u8"(none)", settings != nullptr ? settings->defaultInputMapId : Guid{});
            // The cooked mixer applied at startup; nil = the built-in neutral four-bus layout.
            AddAssetRow(*column, u8"Default bus layout", m_busLayoutId, u8"AudioBusLayoutAsset",
                        u8"(built-in)",
                        settings != nullptr ? settings->defaultBusLayoutId : Guid{});
            // The cooked UITheme the game UI defaults to; nil = the built-in GameTheme.
            AddAssetRow(*column, u8"Default UI theme", m_uiThemeId, u8"UIThemeAsset",
                        u8"(built-in)", settings != nullptr ? settings->defaultUiThemeId : Guid{});
            // The cooked UIDocument shown as the boot splash while the default scene streams;
            // nil = the built-in default (status + progress ids).
            AddAssetRow(*column, u8"Loading screen", m_loadingDocId, u8"UIDocumentAsset",
                        u8"(built-in)", settings != nullptr ? settings->loadingDocumentId : Guid{});
            // The cooked font the game UI falls back to when a document names none (nil renders
            // no game UI text in a real project until this is set).
            AddAssetRow(*column, u8"Default UI font", m_uiFontId, u8"FontAsset", u8"(built-in)",
                        settings != nullptr ? settings->defaultUiFontId : Guid{});

            // Scene-pass MSAA: Off / 2x / 4x maps to renderMsaaSamples 1 / 2 / 4. The
            // player and play-in-editor apply it; the render subsystem capability-clamps at runtime
            // (2x degrades to 1x on WebGPU).
            {
                ui::FlexLayout* row = AddRow(*column, u8"MSAA");
                m_msaaCombo = MakeRef<ui::ComboBox>(MemoryAllocator());
                for (u32 i = 0; i < engine::render::MsaaLevelCount(); ++i)
                {
                    (void)m_msaaCombo->AddItem(engine::render::kMsaaLevels[i].label);
                }
                const u32 samples = (project != nullptr) ? project->Settings().renderMsaaSamples : 1u;
                m_msaaCombo->SetSelectedIndex(engine::render::MsaaIndexForSamples(samples));
                ui::LayoutStyle lp;
                lp.FlexGrow = 1.0f;
                row->AddView(m_msaaCombo.Get(), lp);
            }

            // Engine stamp - informational; re-stamped by every save.
            {
                ui::FlexLayout* row = AddRow(*column, u8"Engine version");
                auto value =
                    MakeRef<ui::Label>(MemoryAllocator(), editor::kEngineVersionString);
                ui::LayoutStyle lp;
                lp.AlignSelf = ui::Align::Center;
                row->AddView(value.Get(), lp);
            }

            // Scroll the settings column so a tall list can't spill over the modal button row
            // (the Dialog gives its content a fixed, Grow-shared area above the buttons; without
            // scrolling, a column taller than that area overflows onto them). User feedback.
            auto scroll = MakeRef<ui::ScrollView>(MemoryAllocator());
            scroll->VScrollBarPolicy.SetValue(ui::ScrollBarPolicy::Auto);
            scroll->HScrollBarPolicy.SetValue(ui::ScrollBarPolicy::Never);
            {
                ui::LayoutStyle lp;
                lp.Width = ui::SizeSpec::Match();
                scroll->AddView(column.Get(), lp);
            }
            SetContent(scroll.Get());

            {
                ProjectSettingsDialog* self = this;
                ui::Button* save = AddButton(u8"Save", ui::DialogResult::None);
                save->OnClick.Add([self](ui::ButtonBase*) { self->Apply(); });
            }
            AddButton(u8"Cancel", ui::DialogResult::Cancel);
        }

    private:
        // A labeled horizontal row (fixed-width label, callers append the field views).
        ui::FlexLayout* AddRow(ui::FlexLayout& column, StringView label);

        ui::EditText* AddTextRow(ui::FlexLayout& column, StringView label, StringView value);

        /// An asset setting's row: a slot bound to `id` (the value Save applies). Edit and
        /// reveal are left off: this is a modal dialog.
        void AddAssetRow(ui::FlexLayout& column, StringView label, Guid& id, StringView typeName,
                         StringView emptyText, const Guid& current);

        void Apply();

        editor::EditorContext* m_context;
        Guid m_inputMapId{};
        Guid m_busLayoutId{};
        Guid m_uiThemeId{};
        Guid m_loadingDocId{};
        Guid m_uiFontId{};
        Array<RefPtr<ResourceRefEditor>> m_assetRows; // the rows' editors; their views sit in rows
        ui::EditText* m_nameEdit = nullptr;
        ui::EditText* m_nativeModuleEdit = nullptr; // project-relative path; empty = none
        Guid m_scriptId{};
        Guid m_sceneId;
        RefPtr<ui::ComboBox> m_msaaCombo; // scene-pass MSAA: Off/2x/4x -> renderMsaaSamples 1/2/4
    };

    RTTI_DEFINE_OBJECT(ProjectSettingsDialog, "rtti::editor::editor::app")
}
