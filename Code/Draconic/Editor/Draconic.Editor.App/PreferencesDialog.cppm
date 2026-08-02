// Draconic::EditorApp - :preferences_dialog partition.
//
// EditorPreferencesDialog: a modal editor for PER-USER editor preferences (the draconic.settings
// store persisted at <user-data>/editor.settings.xml) - distinct from ProjectSettingsDialog, which
// edits the project manifest. Fields: the export templates root (blank = "$DRACONIC_TEMPLATES_DIR,
// else <user-data>/templates", shown as the placeholder) and the editor FONT paths (blank = the
// built-in chain: dev-tree face, then the exe-embedded fallback). [Save] writes the sections back
// into the store and persists it; font changes apply on the next editor start. [Cancel] discards.

module;
#include "Draconic.Core/Prelude.h"
#include "Draconic.Core/Reflection/Reflect.h"

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
            MinHeight.SetValue(220.0f);
            MaxWidth.SetValue(640.0f);
            MaxHeight.SetValue(300.0f);

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

            StringView fontPath;
            StringView monoPath;
            if (const draconic::editor::EditorFontSettings* f =
                    store.Find<draconic::editor::EditorFontSettings>())
            {
                fontPath = f->fontPath.AsView();
                monoPath = f->monoFontPath.AsView();
            }
            m_fontEdit = AddTextRow(*column, u8"UI font (.ttf)", fontPath);
            m_fontEdit->SetPlaceholder(u8"built-in (embedded fallback)");
            m_monoFontEdit = AddTextRow(*column, u8"Mono font (.ttf)", monoPath);
            m_monoFontEdit->SetPlaceholder(u8"built-in");
            {
                auto note = MakeRef<ui::Label>(DefaultAllocator(),
                                               StringView(u8"Font changes apply on restart."));
                note->FontSize.SetValue(11.0f);
                note->TextColor.SetValue(Optional<Color>(Color{0.55f, 0.55f, 0.55f, 1.0f}));
                column->AddView(note.Get());
            }

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
            draconic::editor::EditorFontSettings& fontPrefs =
                m_settings->Section<draconic::editor::EditorFontSettings>();
            fontPrefs.fontPath = String(m_fontEdit->Text());
            fontPrefs.monoFontPath = String(m_monoFontEdit->Text());
            m_settings->MarkChanged<draconic::editor::EditorFontSettings>();
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
        ui::EditText* m_fontEdit = nullptr;
        ui::EditText* m_monoFontEdit = nullptr;
    };

    DRACONIC_DEFINE_OBJECT(EditorPreferencesDialog, "draconic::editor::app")
}
