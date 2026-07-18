// Draconic::EditorApp - :settings_dialog partition.
//
// ProjectSettingsDialog: a modal editor for the project manifest (Project.xml) - the fields a
// user meaningfully changes from inside the editor: project name, the default scene (picked
// through the guid-authoritative AssetPickerDialog, filtered to scenes), and the startup game
// script path. The engine version stamp is shown read-only (every save re-stamps it to the
// running engine; the launcher/project-manager owns migration).
//
// [Save] writes the fields back into EditorProject::Settings() and persists the manifest;
// [Cancel]/Escape discards. The default-scene pick stores the instance GUID (rename/move-proof)
// with the path kept alongside as the human-readable mirror; the picker's [Clear] sets "none".

module;
#include "Core/Prelude.h"
#include "Core/Log/Log.h"
#include "Core/Reflection/Reflect.h"

export module draconic.editor.app:settings_dialog;

import draconic.core;
import draconic.content;
import draconic.ui;
import draconic.editor.core;
import :asset_picker_dialog;

using namespace draconic::core;

export namespace draconic::editor::app
{
    namespace ui = draconic::ui;
    namespace content = draconic::content;

    class ProjectSettingsDialog final : public ui::Dialog
    {
        DRACONIC_OBJECT(ProjectSettingsDialog, ui::Dialog)
    public:
        explicit ProjectSettingsDialog(draconic::editor::EditorContext& context)
            : ui::Dialog(u8"Project Settings"), m_context(&context)
        {
            MinWidth.SetValue(460.0f);
            MinHeight.SetValue(240.0f);
            MaxWidth.SetValue(560.0f);
            MaxHeight.SetValue(320.0f);

            draconic::editor::EditorProject* project = context.Project();

            auto column = MakeRef<ui::FlexLayout>(DefaultAllocator());
            column->Direction = ui::Orientation::Vertical;
            column->Spacing = 8;

            m_nameEdit = AddTextRow(*column, u8"Name", project != nullptr
                ? project->Settings().name.AsView() : StringView(u8""));

            // Default scene: read-only path + [Pick...] (the picker owns clearing too).
            {
                ui::FlexLayout* row = AddRow(*column, u8"Default scene");
                m_sceneLabel = MakeRef<ui::Label>(DefaultAllocator(), StringView(u8"(none)"));
                {
                    auto lp = MakeRef<ui::FlexLayoutParams>(DefaultAllocator());
                    lp->Grow = 1.0f;
                    lp->AlignSelf = ui::Align::Center;
                    row->AddView(m_sceneLabel.Get(), lp);
                }
                m_pickButton = MakeRef<ui::Button>(DefaultAllocator(), StringView(u8"Pick..."));
                {
                    ProjectSettingsDialog* self = this;
                    m_pickButton->OnClick.Add([self](ui::ButtonBase*) { self->PickScene(); });
                    row->AddView(m_pickButton.Get());
                }
                if (project != nullptr)
                {
                    m_sceneId = project->Settings().defaultSceneId;
                    // Prefer the live instance's path over the stored mirror (never lies).
                    if (content::Instance* scene = !m_sceneId.IsNil()
                            ? project->SourceDb().GetInstance(m_sceneId) : nullptr)
                    {
                        m_sceneLabel->SetText(scene->Path().AsView());
                    }
                    else if (!project->Settings().defaultScene.IsEmpty())
                    {
                        m_sceneLabel->SetText(project->Settings().defaultScene.AsView());
                    }
                }
            }

            m_scriptEdit = AddTextRow(*column, u8"Startup script", project != nullptr
                ? project->Settings().startupScript.AsView() : StringView(u8""));

            // Default input map: the cooked map the player (and the Game tab) binds at
            // startup - the input twin of the default scene.
            {
                ui::FlexLayout* row = AddRow(*column, u8"Default input map");
                m_inputMapLabel = MakeRef<ui::Label>(DefaultAllocator(), StringView(u8"(none)"));
                {
                    auto lp = MakeRef<ui::FlexLayoutParams>(DefaultAllocator());
                    lp->Grow = 1.0f;
                    lp->AlignSelf = ui::Align::Center;
                    row->AddView(m_inputMapLabel.Get(), lp);
                }
                auto pick = MakeRef<ui::Button>(DefaultAllocator(), StringView(u8"Pick..."));
                {
                    ProjectSettingsDialog* self = this;
                    pick->OnClick.Add([self](ui::ButtonBase*) { self->PickInputMap(); });
                    row->AddView(pick.Get());
                }
                auto clear = MakeRef<ui::Button>(DefaultAllocator(), StringView(u8"Clear"));
                {
                    ProjectSettingsDialog* self = this;
                    clear->OnClick.Add([self](ui::ButtonBase*) {
                        self->m_inputMapId = Guid{};
                        self->m_inputMapLabel->SetText(u8"(none)");
                    });
                    row->AddView(clear.Get());
                }
                if (project != nullptr)
                {
                    m_inputMapId = project->Settings().defaultInputMapId;
                    if (content::Instance* map = !m_inputMapId.IsNil()
                            ? project->SourceDb().GetInstance(m_inputMapId) : nullptr)
                    {
                        m_inputMapLabel->SetText(map->Path().AsView());
                    }
                }
            }

            // Engine stamp - informational; re-stamped by every save.
            {
                ui::FlexLayout* row = AddRow(*column, u8"Engine version");
                auto value = MakeRef<ui::Label>(DefaultAllocator(),
                                                draconic::editor::kEngineVersionString);
                auto lp = MakeRef<ui::FlexLayoutParams>(DefaultAllocator());
                lp->AlignSelf = ui::Align::Center;
                row->AddView(value.Get(), lp);
            }

            SetContent(column.Get());

            {
                ProjectSettingsDialog* self = this;
                ui::Button* save = AddButton(u8"Save", ui::DialogResult::None);
                save->OnClick.Add([self](ui::ButtonBase*) { self->Apply(); });
            }
            AddButton(u8"Cancel", ui::DialogResult::Cancel);
        }

    private:
        // A labeled horizontal row (fixed-width label, callers append the field views).
        ui::FlexLayout* AddRow(ui::FlexLayout& column, StringView label)
        {
            auto row = MakeRef<ui::FlexLayout>(DefaultAllocator());
            row->Direction = ui::Orientation::Horizontal;
            row->Spacing = 8;
            {
                auto text = MakeRef<ui::Label>(DefaultAllocator(), label);
                auto lp = MakeRef<ui::FlexLayoutParams>(DefaultAllocator());
                lp->Width = ui::SizeSpec::Fixed(ui::Unit::Px(110));
                lp->AlignSelf = ui::Align::Center;
                row->AddView(text.Get(), lp);
            }
            ui::FlexLayout* raw = row.Get();
            {
                auto lp = MakeRef<ui::FlexLayoutParams>(DefaultAllocator());
                lp->Width = ui::SizeSpec::Match();
                column.AddView(row.Get(), lp);
            }
            return raw;
        }

        ui::EditText* AddTextRow(ui::FlexLayout& column, StringView label, StringView value)
        {
            ui::FlexLayout* row = AddRow(column, label);
            auto edit = MakeRef<ui::EditText>(DefaultAllocator());
            edit->SetText(value);
            ui::EditText* raw = edit.Get();
            auto lp = MakeRef<ui::FlexLayoutParams>(DefaultAllocator());
            lp->Grow = 1.0f;
            row->AddView(edit.Get(), lp);
            return raw;
        }

        void PickInputMap()
        {
            if (Context == nullptr) { return; }
            Array<String> typeNames;
            typeNames.PushBack(String(u8"InputMapAsset"));
            auto picker = MakeRef<AssetPickerDialog>(DefaultAllocator(), *m_context, Move(typeNames));
            ProjectSettingsDialog* self = this;
            picker->OnPicked = [self](const Guid& id) {
                self->m_inputMapId = id;
                if (content::Instance* map = !id.IsNil() && self->m_context->Project() != nullptr
                        ? self->m_context->Project()->SourceDb().GetInstance(id) : nullptr)
                {
                    self->m_inputMapLabel->SetText(map->Path().AsView());
                }
                else
                {
                    self->m_inputMapLabel->SetText(u8"(none)");
                }
            };
            picker->Show(Context);
        }

        void PickScene()
        {
            if (Context == nullptr) { return; }
            Array<String> typeNames;
            typeNames.PushBack(String(u8"SceneDocument"));
            auto picker = MakeRef<AssetPickerDialog>(DefaultAllocator(), *m_context, Move(typeNames));
            ProjectSettingsDialog* self = this;
            picker->OnPicked = [self](const Guid& id) {
                self->m_sceneId = id;
                if (content::Instance* scene = !id.IsNil() && self->m_context->Project() != nullptr
                        ? self->m_context->Project()->SourceDb().GetInstance(id) : nullptr)
                {
                    self->m_sceneLabel->SetText(scene->Path().AsView());
                }
                else
                {
                    self->m_sceneLabel->SetText(u8"(none)");
                }
            };
            picker->Show(Context);   // stacks above this dialog on the popup layer
        }

        void Apply()
        {
            draconic::editor::EditorProject* project = m_context->Project();
            if (project == nullptr)
            {
                Close(ui::DialogResult::Cancel);
                return;
            }
            project->Settings().name = String(m_nameEdit->Text());
            project->Settings().startupScript = String(m_scriptEdit->Text());
            project->Settings().defaultSceneId = m_sceneId;
            project->Settings().defaultInputMapId = m_inputMapId;
            project->Settings().defaultScene = String();
            if (content::Instance* scene = !m_sceneId.IsNil()
                    ? project->SourceDb().GetInstance(m_sceneId) : nullptr)
            {
                project->Settings().defaultScene = scene->Path();
            }
            if (project->SaveSettings().IsOk())
            {
                m_context->SetStatus(u8"Project settings saved.");
                DRACONIC_LOG_INFO(u8"Project", u8"settings saved (default scene: {})",
                                  project->Settings().defaultScene.IsEmpty()
                                      ? StringView(u8"(none)")
                                      : project->Settings().defaultScene.AsView());
            }
            else
            {
                m_context->Notify(draconic::editor::NoticeKind::Error,
                                  u8"Project settings save FAILED (see console).");
            }
            Close(ui::DialogResult::OK);
        }

        draconic::editor::EditorContext* m_context;
        Guid m_inputMapId{};
        RefPtr<ui::Label> m_inputMapLabel;
        ui::EditText* m_nameEdit = nullptr;
        ui::EditText* m_scriptEdit = nullptr;
        RefPtr<ui::Label> m_sceneLabel;
        RefPtr<ui::Button> m_pickButton;
        Guid m_sceneId;
    };

    DRACONIC_DEFINE_OBJECT(ProjectSettingsDialog, "draconic::editor::app")
}
