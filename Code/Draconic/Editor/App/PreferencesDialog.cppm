// Draconic::EditorApp - :preferences_dialog partition.
//
// EditorPreferencesDialog: a modal editor for PER-USER editor preferences (the draconic.settings
// store persisted at <user-data>/editor.settings.xml) - distinct from ProjectSettingsDialog, which
// edits the project manifest. Currently one field: the export templates root. Blank means "resolve
// from $DRACONIC_TEMPLATES_DIR, else the built-in <user-data>/templates" - shown as the field's
// placeholder so the fallback is visible. [Save] writes the EditorExportSettings section back into
// the store and persists it; [Cancel]/Escape discards.

module;
#include "Core/Prelude.h"
#include "Core/Reflection/Reflect.h"

export module draconic.editor.app:preferences_dialog;

import draconic.core;
import draconic.ui;
import draconic.editor.core;
import draconic.settings;

using namespace draconic::core;

export namespace draconic::editor::app
{
    namespace ui = draconic::ui;
    namespace settings = draconic::settings;

    class EditorPreferencesDialog final : public ui::Dialog
    {
        DRACONIC_OBJECT(EditorPreferencesDialog, ui::Dialog)
    public:
        EditorPreferencesDialog(draconic::editor::EditorContext& context, settings::Settings& store)
            : ui::Dialog(u8"Preferences"), m_context(&context), m_settings(&store)
        {
            MinWidth.SetValue(480.0f);
            MinHeight.SetValue(150.0f);
            MaxWidth.SetValue(640.0f);
            MaxHeight.SetValue(210.0f);

            auto column = MakeRef<ui::FlexLayout>(DefaultAllocator());
            column->Direction = ui::Orientation::Vertical;
            column->Spacing = 8;

            StringView current;
            if (const draconic::editor::EditorExportSettings* s =
                    store.Find<draconic::editor::EditorExportSettings>())
            {
                current = s->templatesRoot.AsView();
            }
            m_rootEdit = AddTextRow(*column, u8"Templates root", current);
            m_rootEdit->SetPlaceholder(draconic::editor::DefaultTemplatesRoot().AsView());

            SetContent(column.Get());

            {
                EditorPreferencesDialog* self = this;
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

        void Apply()
        {
            m_settings->Section<draconic::editor::EditorExportSettings>().templatesRoot =
                String(m_rootEdit->Text());
            m_settings->MarkChanged<draconic::editor::EditorExportSettings>();
            if (draconic::editor::SaveEditorSettingsToUserData(*m_settings).IsOk())
            {
                m_context->SetStatus(u8"Preferences saved.");
            }
            else
            {
                m_context->Notify(draconic::editor::NoticeKind::Error,
                                  u8"Preferences save FAILED (see console).");
            }
            Close(ui::DialogResult::OK);
        }

        draconic::editor::EditorContext* m_context;
        settings::Settings* m_settings;
        ui::EditText* m_rootEdit = nullptr;
    };

    DRACONIC_DEFINE_OBJECT(EditorPreferencesDialog, "draconic::editor::app")
}
