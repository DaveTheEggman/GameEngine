// SPDX-License-Identifier: MIT
// Copyright (c) 2026-Present Robert Campbell

// Editor::App - :preferences_dialog partition.
//
// EditorPreferencesDialog: a modal editor for PER-USER editor preferences (the foundation.settings
// store persisted at <user-data>/editor.settings.xml) - distinct from ProjectSettingsDialog, which
// edits the project manifest. Fields: the export templates root (blank = "$ENV_TEMPLATES_DIR,
// else <user-data>/templates", shown as the placeholder) and the editor FONT paths (blank = the
// built-in chain: dev-tree face, then the exe-embedded fallback). [Save] writes the sections back
// into the store and persists it; font changes apply on the next editor start. [Cancel] discards.

module;
#include "Core/Prelude.h"
#include "Core/Reflection/Reflect.h"

export module editor.app:preferences_dialog;

import foundation.core;
import foundation.ui;
import editor.core;
import foundation.settings;
import :shortcut_capture;

using namespace foundation::core;

export namespace editor::app
{
    namespace ui = foundation::ui;
    namespace settings = foundation::settings;

    class EditorPreferencesDialog final : public ui::Dialog
    {
        RTTI_OBJECT(EditorPreferencesDialog, ui::Dialog)
    public:
        EditorPreferencesDialog(editor::EditorContext& context, settings::Settings& store)
            : ui::Dialog(u8"Preferences"), m_context(&context), m_settings(&store)
        {
            MinWidth.SetValue(560.0f);
            MinHeight.SetValue(220.0f);
            MaxWidth.SetValue(760.0f);
            MaxHeight.SetValue(560.0f);

            auto column = MakeRef<ui::FlexLayout>(MemoryAllocator());
            column->Direction = ui::Orientation::Vertical;
            column->Spacing = 8;

            StringView current;
            if (const editor::EditorExportSettings* s =
                    store.Find<editor::EditorExportSettings>())
            {
                current = s->templatesRoot.AsView();
            }
            m_rootEdit = AddTextRow(*column, u8"Templates root", current);
            m_rootEdit->SetPlaceholder(editor::DefaultTemplatesRoot().AsView());

            StringView fontPath;
            StringView monoPath;
            if (const editor::EditorFontSettings* f =
                    store.Find<editor::EditorFontSettings>())
            {
                fontPath = f->fontPath.AsView();
                monoPath = f->monoFontPath.AsView();
            }
            m_fontEdit = AddTextRow(*column, u8"UI font (.ttf)", fontPath);
            m_fontEdit->SetPlaceholder(u8"built-in (embedded fallback)");
            m_monoFontEdit = AddTextRow(*column, u8"Mono font (.ttf)", monoPath);
            m_monoFontEdit->SetPlaceholder(u8"built-in");
            f32 uiScale = 1.0f;
            if (const editor::EditorUiSettings* u =
                    store.Find<editor::EditorUiSettings>())
            {
                uiScale = Clamp(u->uiScale, editor::kUiScaleMin, editor::kUiScaleMax);
            }
            {
                ui::FlexLayout* row = AddRow(*column, u8"UI scale");
                auto slider = MakeRef<ui::Slider>(MemoryAllocator());
                slider->Min.SetValue(editor::kUiScaleMin);
                slider->Max.SetValue(editor::kUiScaleMax);
                slider->Step.SetValue(0.05f);
                slider->Value.SetValue(uiScale);
                m_uiScaleSlider = slider.Get();
                {
                    ui::LayoutStyle lp;
                    lp.FlexGrow = 1.0f;
                    lp.AlignSelf = ui::Align::Center;
                    row->AddView(slider.Get(), lp);
                }
                auto valueLabel = MakeRef<ui::Label>(MemoryAllocator(), StringView(u8"1.00x"));
                valueLabel->FontSize.SetValue(11.0f);
                m_uiScaleLabel = valueLabel.Get();
                {
                    ui::LayoutStyle lp;
                    lp.Width = ui::SizeSpec::Fixed(ui::Unit::Dp(44));
                    lp.AlignSelf = ui::Align::Center;
                    row->AddView(valueLabel.Get(), lp);
                }
                UpdateScaleLabel(uiScale);
                EditorPreferencesDialog* self = this;
                slider->OnValueChanged.Add(
                    ui::Event<void(ui::Slider*, f32)>::Handler{
                        [self](ui::Slider*, f32 v) { self->UpdateScaleLabel(v); }});
            }
            {
                auto note = MakeRef<ui::Label>(MemoryAllocator(),
                                               StringView(u8"Font changes apply on restart."));
                note->FontSize.SetValue(11.0f);
                note->TextColor.SetValue(Optional<Color>(Color{0.55f, 0.55f, 0.55f, 1.0f}));
                column->AddView(note.Get());
            }

            // Agent access: the MCP host over the open project (EditorMcpSettings).
            {
                auto header = MakeRef<ui::Label>(MemoryAllocator(),
                                                 StringView(u8"Agent access (MCP)"));
                header->FontSize.SetValue(13.0f);
                column->AddView(header.Get());
                const editor::EditorMcpSettings* mcp = store.Find<editor::EditorMcpSettings>();
                auto check = MakeRef<ui::CheckBox>(
                    MemoryAllocator(), StringView(u8"Serve the open project to agents"),
                    mcp != nullptr && mcp->enabled);
                check->FontSize.SetValue(12.0f);
                check->TooltipText =
                    String(u8"An MCP host on 127.0.0.1 for the project this editor has open; "
                           u8"the token below is the secret an agent presents");
                m_mcpEnabled = check.Get();
                {
                    ui::LayoutStyle lp;
                    lp.Width = ui::SizeSpec::Match();
                    lp.Height = ui::SizeSpec::Fixed(ui::Unit::Dp(22.0f));
                    column->AddView(check.Get(), lp);
                }
                const u32 port = mcp != nullptr ? mcp->port : editor::kEditorMcpDefaultPort;
                m_mcpPortEdit = AddTextRow(*column, u8"MCP port", Format(u8"{}", port).AsView());
                m_mcpTokenEdit = AddTextRow(*column, u8"MCP token",
                                            mcp != nullptr ? mcp->token.AsView() : StringView());
                m_mcpTokenEdit->SetPlaceholder(u8"minted on first enable");
                auto note = MakeRef<ui::Label>(
                    MemoryAllocator(),
                    StringView(u8"Applies to the open project on Save; the token is also written "
                               u8"to <user-data>/mcp-token for a local agent."));
                note->FontSize.SetValue(11.0f);
                note->TextColor.SetValue(Optional<Color>(Color{0.55f, 0.55f, 0.55f, 1.0f}));
                column->AddView(note.Get());
            }

            // Shortcuts: every action with its effective chord; a click on the chord captures the
            // next key, Reset forgets the override. Staged in m_shortcutEdits, applied on Save.
            {
                auto header = MakeRef<ui::Label>(MemoryAllocator(), StringView(u8"Shortcuts"));
                header->FontSize.SetValue(13.0f);
                column->AddView(header.Get());
                auto note = MakeRef<ui::Label>(
                    MemoryAllocator(),
                    StringView(u8"Click a chord and press the new keys (Esc cancels, Del clears). A "
                               u8"chord another action holds is refused on Save, naming it."));
                note->FontSize.SetValue(11.0f);
                note->TextColor.SetValue(Optional<Color>(Color{0.55f, 0.55f, 0.55f, 1.0f}));
                column->AddView(note.Get());
                EditorActionRegistry& actions = context.Actions();
                for (const editor::EditorActionDeclaration& action : actions.Actions())
                {
                    AddShortcutRow(*column, actions, action);
                }
            }

            // Domain-contributed categories (EditorContext::RegisterEditorSettingsContribution):
            // the app hardcodes nothing - each domain's fields render generically here and
            // write through their own closures (usually into the domain's user-store section).
            for (const editor::EditorContext::EditorSettingsContribution& contribution :
                 context.EditorSettingsContributions())
            {
                auto header = MakeRef<ui::Label>(MemoryAllocator(),
                                                 contribution.category.AsView());
                header->FontSize.SetValue(13.0f);
                column->AddView(header.Get());
                for (const editor::EditorContext::EditorSettingsBoolField& field :
                     contribution.bools)
                {
                    // `checked`, not `current`: the StringView `current` above (the templates
                    // root) is still in scope, and reusing the name is MSVC C4456, fatal under /WX.
                    const bool checked = field.get ? field.get() : false;
                    auto check =
                        MakeRef<ui::CheckBox>(MemoryAllocator(), field.label.AsView(), checked);
                    check->FontSize.SetValue(12.0f);
                    if (!field.description.IsEmpty())
                    {
                        check->TooltipText = String(field.description);
                    }
                    // Function is move-only: capture the FIELD (context-owned, stable -
                    // registrations happen at boot, before any dialog opens).
                    const editor::EditorContext::EditorSettingsBoolField* fieldPtr = &field;
                    check->OnCheckedChanged.Add(
                        [fieldPtr](ui::CheckBox*, bool checked)
                        {
                            if (fieldPtr->set)
                            {
                                fieldPtr->set(checked);
                            }
                        });
                    ui::LayoutStyle lp;
                    lp.Width = ui::SizeSpec::Match();
                    lp.Height = ui::SizeSpec::Fixed(ui::Unit::Dp(22.0f));
                    column->AddView(check.Get(), lp);
                }
            }

            // Scroll the preferences column so it can grow without spilling over the modal button
            // row (the Dialog gives content a fixed Grow-shared area above the buttons). User feedback.
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
                EditorPreferencesDialog* self = this;
                ui::Button* save = AddButton(u8"Save", ui::DialogResult::None);
                save->OnClick.Add([self](ui::ButtonBase*) { self->Apply(); });
            }
            AddButton(u8"Cancel", ui::DialogResult::Cancel);
        }

    public:
        /// Fired on Apply with the new UI scale so the app can apply it LIVE (set the
        /// host's scale + re-bake icons); the saved setting covers the next launch.
        Function<void(f32)> OnUiScaleApplied;
        /// Fired on Apply after the MCP section changed, so the app restarts (or stops) the
        /// host for the open project without a reopen.
        Function<void()> OnMcpSettingsApplied;

    private:
        void UpdateScaleLabel(f32 value)
        {
            if (m_uiScaleLabel != nullptr)
            {
                const i32 percent = static_cast<i32>(value * 100.0f + 0.5f);
                m_uiScaleLabel->SetText(Format(u8"{}%", percent).AsView());
            }
        }

        // A labeled horizontal row (fixed-width label, callers append the field views).
        ui::FlexLayout* AddRow(ui::FlexLayout& column, StringView label)
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

        // One action: its label, its menu path, the chord (a capture button) and Reset.
        void AddShortcutRow(ui::FlexLayout& column, EditorActionRegistry& actions,
                            const editor::EditorActionDeclaration& action)
        {
            auto row = MakeRef<ui::FlexLayout>(MemoryAllocator());
            row->Direction = ui::Orientation::Horizontal;
            row->Spacing = 8;
            {
                auto text = MakeRef<ui::Label>(MemoryAllocator(), action.label.AsView());
                text->FontSize.SetValue(12.0f);
                ui::LayoutStyle lp;
                lp.Width = ui::SizeSpec::Fixed(ui::Unit::Dp(170));
                lp.AlignSelf = ui::Align::Center;
                row->AddView(text.Get(), lp);
            }
            {
                auto where = MakeRef<ui::Label>(MemoryAllocator(), action.menuPath.AsView());
                where->FontSize.SetValue(11.0f);
                where->TextColor.SetValue(Optional<Color>(Color{0.55f, 0.55f, 0.55f, 1.0f}));
                ui::LayoutStyle lp;
                lp.FlexGrow = 1.0f;
                lp.AlignSelf = ui::Align::Center;
                row->AddView(where.Get(), lp);
            }
            const StringView id = action.id.AsView();
            auto capture = MakeRef<ShortcutCaptureButton>(MemoryAllocator(), actions.Shortcut(id));
            capture->FontSize.SetValue(Optional<f32>(11.0f));
            ShortcutCaptureButton* captureRaw = capture.Get();
            {
                ui::LayoutStyle lp;
                lp.Width = ui::SizeSpec::Fixed(ui::Unit::Dp(150));
                lp.AlignSelf = ui::Align::Center;
                row->AddView(capture.Get(), lp);
            }
            auto reset = MakeRef<ui::Button>(MemoryAllocator(), StringView(u8"Reset"));
            reset->FontSize.SetValue(Optional<f32>(11.0f));
            reset->IsEnabled = actions.HasOverride(id);
            ui::Button* resetRaw = reset.Get();
            {
                ui::LayoutStyle lp;
                lp.Width = ui::SizeSpec::Fixed(ui::Unit::Dp(60));
                lp.AlignSelf = ui::Align::Center;
                row->AddView(reset.Get(), lp);
            }
            EditorPreferencesDialog* self = this;
            const String idText(id);
            capture->OnChordChosen = [self, idText, resetRaw](EditorShortcut chord)
            {
                self->m_shortcutEdits.Set(idText.AsView(), chord);
                resetRaw->IsEnabled = true;
            };
            const editor::EditorActionDeclaration* declaration = &action;
            reset->OnClick.Add(
                [self, idText, captureRaw, resetRaw, declaration](ui::ButtonBase*)
                {
                    self->m_shortcutEdits.Reset(idText.AsView());
                    captureRaw->SetChord(declaration->shortcut); // the default, shown at once
                    resetRaw->IsEnabled = false;
                });
            {
                ui::LayoutStyle lp;
                lp.Width = ui::SizeSpec::Match();
                column.AddView(row.Get(), lp);
            }
        }

        ui::EditText* AddTextRow(ui::FlexLayout& column, StringView label, StringView value)
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

        void Apply()
        {
            m_settings->Section<editor::EditorExportSettings>().templatesRoot =
                String(m_rootEdit->Text());
            m_settings->MarkChanged<editor::EditorExportSettings>();
            editor::EditorFontSettings& fontPrefs =
                m_settings->Section<editor::EditorFontSettings>();
            fontPrefs.fontPath = String(m_fontEdit->Text());
            fontPrefs.monoFontPath = String(m_monoFontEdit->Text());
            m_settings->MarkChanged<editor::EditorFontSettings>();
            const f32 uiScale = Clamp(m_uiScaleSlider->Value.Value(), editor::kUiScaleMin, editor::kUiScaleMax);
            m_settings->Section<editor::EditorUiSettings>().uiScale = uiScale;
            m_settings->MarkChanged<editor::EditorUiSettings>();
            if (OnUiScaleApplied)
            {
                OnUiScaleApplied(uiScale); // live: host scale + icon re-bake
            }
            if (!m_settings->Section<editor::EditorMcpSettings>().ApplyFromPreferences(
                    m_mcpEnabled->IsChecked.Value(), m_mcpPortEdit->Text(),
                    m_mcpTokenEdit->Text()))
            {
                m_context->Notify(editor::NoticeKind::Warning,
                                  u8"MCP port must be a number in 1024..65535 - kept the old one.");
            }
            m_settings->MarkChanged<editor::EditorMcpSettings>();
            if (OnMcpSettingsApplied)
            {
                OnMcpSettingsApplied(); // live: the host follows the new enabled/port/token
            }
            if (!m_shortcutEdits.IsEmpty())
            {
                Array<String> collisions;
                (void)m_shortcutEdits.Apply(m_context->Actions(),
                                            m_settings->Section<editor::EditorShortcutSettings>(),
                                            &collisions);
                m_settings->MarkChanged<editor::EditorShortcutSettings>();
                for (const String& collision : collisions)
                {
                    m_context->Notify(editor::NoticeKind::Warning,
                                      Format(u8"Shortcut kept: {}", collision.AsView()).AsView());
                }
            }
            if (editor::SaveEditorSettingsToUserData(*m_settings).IsOk())
            {
                m_context->SetStatus(u8"Preferences saved.");
            }
            else
            {
                m_context->Notify(editor::NoticeKind::Error,
                                  u8"Preferences save FAILED (see console).");
            }
            Close(ui::DialogResult::OK);
        }

        editor::EditorContext* m_context;
        settings::Settings* m_settings;
        ui::EditText* m_rootEdit = nullptr;
        ui::EditText* m_fontEdit = nullptr;
        ui::EditText* m_monoFontEdit = nullptr;
        ui::Slider* m_uiScaleSlider = nullptr;
        ui::Label* m_uiScaleLabel = nullptr;
        ui::CheckBox* m_mcpEnabled = nullptr;
        ui::EditText* m_mcpPortEdit = nullptr;
        ui::EditText* m_mcpTokenEdit = nullptr;
        editor::ShortcutEdits m_shortcutEdits; // staged; applied and persisted on Save
    };

    RTTI_DEFINE_OBJECT(EditorPreferencesDialog, "rtti::editor::editor::app")
}
