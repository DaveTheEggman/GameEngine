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

module editor.app;

import foundation.core;
import foundation.content;
import foundation.ui;
import engine.render; // MsaaSamplesForIndex (the canonical MSAA level mapping)
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

    void ProjectSettingsDialog::AddAssetRow(ui::FlexLayout& column, StringView label, Guid& id,
                                            StringView typeName, StringView emptyText,
                                            const Guid& current)
    {
        ui::FlexLayout* row = AddRow(column, label);
        id = current;
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
        project->Settings().name = String(m_nameEdit->Text());
        project->Settings().nativeModule = String(m_nativeModuleEdit->Text());
        project->Settings().startupScriptId = m_scriptId;
        project->Settings().startupScript =
            String(); // the source-DB path mirror (display / v<6 fallback)
        if (content::Instance* script =
                !m_scriptId.IsNil() ? project->SourceDb().GetInstance(m_scriptId) : nullptr)
        {
            project->Settings().startupScript = script->Path();
        }
        project->Settings().defaultSceneId = m_sceneId;
        project->Settings().defaultInputMapId = m_inputMapId;
        project->Settings().defaultBusLayoutId = m_busLayoutId;
        project->Settings().defaultUiThemeId = m_uiThemeId;
        project->Settings().loadingDocumentId = m_loadingDocId;
        project->Settings().defaultUiFontId = m_uiFontId;
        const i32 msaaIdx = (m_msaaCombo.Get() != nullptr) ? m_msaaCombo->SelectedIndex() : 0;
        project->Settings().renderMsaaSamples = engine::render::MsaaSamplesForIndex(msaaIdx);
        project->Settings().defaultScene = String();
        if (content::Instance* scene =
                !m_sceneId.IsNil() ? project->SourceDb().GetInstance(m_sceneId) : nullptr)
        {
            project->Settings().defaultScene = scene->Path();
        }
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
