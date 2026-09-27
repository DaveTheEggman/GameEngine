// SPDX-License-Identifier: MIT
// Copyright (c) 2026-Present Robert Campbell

// Editor::App - :shortcut_capture partition.
//
// ShortcutCaptureButton: a button that shows a chord and, once clicked, takes the next key the
// user presses as the new one (modifiers normalised to Ctrl / Shift / Alt / Gui, the way a
// declaration spells them). Escape cancels the capture, Backspace or Delete chooses "no
// shortcut", a modifier alone is not a chord, and losing focus cancels. What Preferences >
// Shortcuts puts in every row.
module;
#include "Core/Prelude.h"
#include "Core/Reflection/Reflect.h"

export module editor.app:shortcut_capture;

import foundation.core;
import foundation.ui;
import editor.core;

using namespace foundation::core;

export namespace editor::app
{
    namespace ui = foundation::ui;

    class ShortcutCaptureButton final : public ui::Button
    {
        RTTI_OBJECT(ShortcutCaptureButton, ui::Button)
    public:
        explicit ShortcutCaptureButton(EditorShortcut chord) : ui::Button(StringView()), m_chord(chord)
        {
            ShowChord();
            ShortcutCaptureButton* self = this;
            OnClick.Add([self](ui::ButtonBase*) { self->BeginCapture(); });
        }

        /// Fired with the chord the user chose (unset = no shortcut). The button shows it.
        Function<void(EditorShortcut)> OnChordChosen;

        [[nodiscard]] EditorShortcut Chord() const noexcept { return m_chord; }
        void SetChord(EditorShortcut chord)
        {
            m_chord = chord;
            m_capturing = false;
            ShowChord();
        }
        [[nodiscard]] bool IsCapturing() const noexcept { return m_capturing; }

        void BeginCapture()
        {
            if (m_capturing)
            {
                return;
            }
            m_capturing = true;
            SetText(u8"Press a chord... (Esc cancels, Del clears)");
            if (Context != nullptr)
            {
                Context->GetFocusManager()->SetFocus(this);
            }
        }
        void CancelCapture()
        {
            if (!m_capturing)
            {
                return;
            }
            m_capturing = false;
            ShowChord();
        }

        void OnKeyDown(ui::KeyEventArgs& e) override
        {
            if (!m_capturing)
            {
                ui::Button::OnKeyDown(e); // Return / Space click, which begins a capture
                return;
            }
            e.Handled = true;
            switch (e.Key)
            {
            case ui::KeyCode::Escape:
                CancelCapture();
                return;
            case ui::KeyCode::Backspace:
            case ui::KeyCode::Delete:
                Choose(EditorShortcut{});
                return;
            case ui::KeyCode::LeftCtrl:
            case ui::KeyCode::LeftShift:
            case ui::KeyCode::LeftAlt:
            case ui::KeyCode::LeftGui:
            case ui::KeyCode::RightCtrl:
            case ui::KeyCode::RightShift:
            case ui::KeyCode::RightAlt:
            case ui::KeyCode::RightGui:
                return; // a modifier alone is not a chord: keep waiting
            default:
                Choose(EditorShortcut{e.Key, ui::Shortcut::Normalize(e.Modifiers)});
                return;
            }
        }
        void OnFocusLost() override
        {
            CancelCapture();
            ui::Button::OnFocusLost();
        }

    private:
        void Choose(EditorShortcut chord)
        {
            m_capturing = false;
            m_chord = chord;
            ShowChord();
            if (OnChordChosen)
            {
                OnChordChosen(chord);
            }
        }
        void ShowChord()
        {
            const String text = FormatShortcut(m_chord);
            SetText(text.IsEmpty() ? StringView(u8"(none)") : text.AsView());
        }

        EditorShortcut m_chord;
        bool m_capturing = false;
    };

    RTTI_DEFINE_OBJECT(ShortcutCaptureButton, "rtti::editor::editor::app")
}
