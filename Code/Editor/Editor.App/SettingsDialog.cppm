// SPDX-License-Identifier: MIT
// Copyright (c) 2026-Present Robert Campbell

// Editor::App - :settings_dialog partition.
//
// ProjectSettingsDialog: a modal editor for the project manifest (Project.xml) - the settings
// ProjectSettings' reflection describes (its name, native module and asset settings, each asset
// slot filtered to the type the setting names) and the MSAA level. The engine version stamp is shown read-only (every save re-stamps it to the
// running engine; the launcher/project-manager owns migration).
//
// [Save] writes the fields back into EditorProject::Settings() and persists the manifest;
// [Cancel]/Escape discards. Every asset setting is a ResourceRefEditor row: pick, drop and clear
// set the instance GUID (rename/move-proof); Save keeps the path alongside as a mirror. An asset
// list setting (the other UI fonts) is a ContainerListEditor row of slots.

module;
#include "Core/Prelude.h"
#include "Core/Log/Log.h"
#include "Core/Reflection/Reflect.h"

export module editor.app:settings_dialog;

import foundation.core;
import foundation.content;
import foundation.ui;
import engine.render; // the canonical MSAA level table (kMsaaLevels + index<->samples helpers)
import engine.project; // ProjectSettings' reflected settings
import editor.core;
import :resource_ref_editor;
import :container_list_editor;
import :asset_picker_dialog;

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

            // The settings as the type describes them (ProjectSettings' reflection): a text row
            // per string setting, and per asset setting a slot that picks, takes a dropped asset
            // of its type and clears, seeded from the manifest (editor-lists-and-asset-slots.md
            // P1). MSAA, a choice from the render subsystem's levels, follows.
            BuildSettingRows(*column, project);

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

        /// One row per reflected setting a row edits: strings as text, asset settings as slots.
        void BuildSettingRows(ui::FlexLayout& column, editor::EditorProject* project);

        /// An asset setting's row: a slot bound to `id` (the value Save applies). Edit and
        /// reveal are left off: this is a modal dialog.
        void AddAssetRow(ui::FlexLayout& column, StringView label, Guid& id, StringView typeName,
                         StringView emptyText);

        /// An asset list setting's row: a list of slots (add, pick, drop, reorder, remove) over
        /// `m_assetLists[index]`, rebuilt after every change.
        void AddAssetListRow(ui::FlexLayout& column, StringView label, usize index);
        void RebuildAssetList(usize index);
        /// After the gesture that changed list `index`: its editor is running the callback, so it
        /// is replaced once the dispatch is over.
        void AssetListChanged(usize index);

        void Apply();

        /// A string setting's row and the reflected property Save writes it to.
        struct TextSetting
        {
            const PropertyInfo* property = nullptr;
            ui::EditText* edit = nullptr;
        };
        /// An asset setting: the reflected property and the value its slot holds.
        struct AssetSetting
        {
            const PropertyInfo* property = nullptr;
            Guid id;
        };

        /// An asset list setting: the reflected property, the list as edited (nil entries are
        /// slots not yet picked; Save drops them), the cell its editor sits in, and the editor.
        struct AssetListSetting
        {
            const PropertyInfo* property = nullptr;
            String assetType;
            Array<Guid> ids;
            ui::FlexLayout* host = nullptr;
            RefPtr<ContainerListEditor> list;
        };

        editor::EditorContext* m_context;
        Array<TextSetting> m_texts;
        Array<AssetSetting> m_assets; // sized before the rows bind to it: never reallocates after
        Array<RefPtr<ResourceRefEditor>> m_assetRows; // the rows' editors; their views sit in rows
        Array<AssetListSetting> m_assetLists; // sized before the rows build: indices stay put
        RefPtr<ui::ComboBox> m_msaaCombo; // scene-pass MSAA: Off/2x/4x -> renderMsaaSamples 1/2/4
    };

    RTTI_DEFINE_OBJECT(ProjectSettingsDialog, "rtti::editor::editor::app")
}
