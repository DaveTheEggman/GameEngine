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
// set the instance GUID (rename/move-proof); Save keeps the path alongside as a mirror.

module;
#include "Core/Prelude.h"
#include "Core/Log/Log.h"
#include "Core/Reflection/Reflect.h"

module editor.app;

import foundation.core;
import foundation.content;
import foundation.ui;
import engine.render; // MsaaSamplesForIndex (the canonical MSAA level mapping)
import engine.project; // ProjectSettings' reflected settings
import editor.core;
import :resource_ref_editor;

using namespace foundation::core;
namespace content = foundation::content;
namespace ui = foundation::ui;

namespace editor::app
{
    ui::FlexLayout* ProjectSettingsDialog::AddRow(ui::FlexLayout& column, StringView label)
    {
        auto row = MakeRef<ui::FlexLayout>(MemoryAllocator());
        row->Direction = ui::Orientation::Horizontal;
        row->Spacing = 8;
        {
            auto text = MakeRef<ui::Label>(MemoryAllocator(), label);
            ui::LayoutStyle lp;
            lp.Width = ui::SizeSpec::Fixed(ui::Unit::Dp(110));
            lp.AlignSelf = ui::Align::Center;
            row->AddView(text.Get(), lp);
        }
        ui::FlexLayout* raw = row.Get();
        {
            ui::LayoutStyle lp;
            lp.Width = ui::SizeSpec::Match();
            column.AddView(row.Get(), lp);
        }
        return raw;
    }

    ui::EditText* ProjectSettingsDialog::AddTextRow(ui::FlexLayout& column, StringView label,
                                                    StringView value)
    {
        ui::FlexLayout* row = AddRow(column, label);
        auto edit = MakeRef<ui::EditText>(MemoryAllocator());
        edit->SetText(value);
        ui::EditText* raw = edit.Get();
        ui::LayoutStyle lp;
        lp.FlexGrow = 1.0f;
        row->AddView(edit.Get(), lp);
        return raw;
    }

    void ProjectSettingsDialog::BuildSettingRows(ui::FlexLayout& column,
                                                 editor::EditorProject* project)
    {
        namespace proj = engine::project;
        const TypeInfo& type = proj::ProjectSettings::StaticType();
        const Instance settings(project != nullptr ? &project->Settings() : nullptr, &type);
        // The asset settings first, all of them, so the slots bind to entries that stay put.
        for (const PropertyInfo& property : Properties(type))
        {
            if (proj::SettingAttribute(property, proj::kSettingAssetTypeAttribute) != nullptr)
            {
                AssetSetting entry;
                entry.property = &property;
                if (project != nullptr)
                {
                    entry.id = *static_cast<const Guid*>(property.address(settings));
                }
                m_assets.PushBack(entry);
            }
        }
        usize asset = 0;
        for (const PropertyInfo& property : Properties(type))
        {
            const String* label = proj::SettingAttribute(property, proj::kSettingLabelAttribute);
            if (label == nullptr)
            {
                continue;
            }
            if (const String* assetType =
                    proj::SettingAttribute(property, proj::kSettingAssetTypeAttribute))
            {
                const String* emptyText =
                    proj::SettingAttribute(property, proj::kSettingEmptyTextAttribute);
                AddAssetRow(column, label->AsView(), m_assets[asset++].id, assetType->AsView(),
                            emptyText != nullptr ? emptyText->AsView() : StringView(u8"(none)"));
            }
            else if (property.type == &TypeOf<String>())
            {
                const StringView value =
                    project != nullptr
                        ? static_cast<const String*>(property.address(settings))->AsView()
                        : StringView(u8"");
                m_texts.PushBack(TextSetting{&property, AddTextRow(column, label->AsView(), value)});
            }
        }
    }

    void ProjectSettingsDialog::AddAssetRow(ui::FlexLayout& column, StringView label, Guid& id,
                                            StringView typeName, StringView emptyText)
    {
        ui::FlexLayout* row = AddRow(column, label);
        const StringView types[] = {typeName};
        auto editor = MakeRef<ResourceRefEditor>(MemoryAllocator(), label, emptyText, StringView{},
                                                 Span<const StringView>{types, 1});
        editor->SetEmptyText(emptyText);
        Guid* target = &id;
        editor->BindAsset(*m_context, [target]() { return *target; },
                          [target](const Guid& picked) { *target = picked; },
                          ResourceRefEditor::BindOptions{.edit = false, .reveal = false});
        ui::LayoutStyle grow;
        grow.FlexGrow = 1.0f;
        grow.AlignSelf = ui::Align::Center;
        row->AddView(editor->EditorView(), grow);
        m_assetRows.PushBack(Move(editor));
    }

    void ProjectSettingsDialog::Apply()
    {
        editor::EditorProject* project = m_context->Project();
        if (project == nullptr)
        {
            Close(ui::DialogResult::Cancel);
            return;
        }
        const Instance settings(&project->Settings(),
                                &engine::project::ProjectSettings::StaticType());
        for (const TextSetting& text : m_texts)
        {
            *static_cast<String*>(text.property->address(settings)) = String(text.edit->Text());
        }
        for (const AssetSetting& asset : m_assets)
        {
            *static_cast<Guid*>(asset.property->address(settings)) = asset.id;
        }
        const i32 msaaIdx = (m_msaaCombo.Get() != nullptr) ? m_msaaCombo->SelectedIndex() : 0;
        project->Settings().renderMsaaSamples = engine::render::MsaaSamplesForIndex(msaaIdx);
        // The source-DB path mirrors beside the guids (display / v<6 fallback).
        project->Settings().RefreshPathMirrors(
            [project](const Guid& id)
            {
                content::Instance* instance = project->SourceDb().GetInstance(id);
                return instance != nullptr ? instance->Path() : String();
            });
        if (project->SaveSettings().IsOk())
        {
            m_context->SetStatus(u8"Project settings saved.");
            // Re-apply settings-derived session state NOW (default UI font/theme binds) -
            // without this, a changed default font kept the OLD bind until project reopen.
            m_context->NotifyProjectSettingsChanged();
            LOG_INFO(u8"Project", u8"settings saved (default scene: {})",
                              project->Settings().defaultScene.IsEmpty()
                                  ? StringView(u8"(none)")
                                  : project->Settings().defaultScene.AsView());
        }
        else
        {
            m_context->Notify(editor::NoticeKind::Error,
                              u8"Project settings save FAILED (see console).");
        }
        Close(ui::DialogResult::OK);
    }
}
