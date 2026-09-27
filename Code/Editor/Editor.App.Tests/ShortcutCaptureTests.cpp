// SPDX-License-Identifier: MIT
// Copyright (c) 2026-Present Robert Campbell

// The chord capture button: a click begins a capture, the next non-modifier key is the chord
// (modifiers normalised), Escape cancels, Delete chooses none, a modifier alone keeps waiting,
// and losing focus cancels.

#include <doctest/doctest.h>
#include "Core/Prelude.h"

import foundation.core;
import foundation.ui;
import editor.core;
import editor.app;

using namespace foundation::core;
using namespace editor;
namespace ui = foundation::ui;

TEST_CASE("shortcut-capture: click, press, chosen - with the cancel, clear, modifier-alone and focus-lost rules")
{
    const EditorShortcut ctrlS{ui::KeyCode::S, ui::KeyModifiers::Ctrl};
    auto button = MakeRef<app::ShortcutCaptureButton>(DefaultAllocator(), ctrlS);
    CHECK(button->Text.Value() == u8"Ctrl+S");
    CHECK_FALSE(button->IsCapturing());
    Array<EditorShortcut> chosen;
    button->OnChordChosen = [&chosen](EditorShortcut chord) { chosen.PushBack(chord); };

    // A key while idle is the button's (Return clicks, which begins a capture).
    ui::KeyEventArgs enter;
    enter.Key = ui::KeyCode::Return;
    button->OnKeyDown(enter);
    CHECK(button->IsCapturing());
    CHECK(button->Text.Value().AsView().StartsWith(u8"Press a chord"));

    // A modifier alone keeps waiting; the next key with LEFT ctrl held is Ctrl+K, normalised.
    ui::KeyEventArgs ctrlAlone;
    ctrlAlone.Key = ui::KeyCode::LeftCtrl;
    ctrlAlone.Modifiers = ui::KeyModifiers::LeftCtrl;
    button->OnKeyDown(ctrlAlone);
    CHECK(ctrlAlone.Handled);
    CHECK(button->IsCapturing());
    CHECK(chosen.IsEmpty());
    ui::KeyEventArgs unknown; // a key the shell could not map: not a chord, keep waiting
    unknown.Key = ui::KeyCode::Unknown;
    unknown.Modifiers = ui::KeyModifiers::LeftCtrl;
    button->OnKeyDown(unknown);
    CHECK(unknown.Handled);
    CHECK(button->IsCapturing());
    CHECK(chosen.IsEmpty());
    ui::KeyEventArgs k;
    k.Key = ui::KeyCode::K;
    k.Modifiers = ui::KeyModifiers::LeftCtrl | ui::KeyModifiers::NumLock; // a lock key is not part of a chord
    button->OnKeyDown(k);
    CHECK(k.Handled);
    CHECK_FALSE(button->IsCapturing());
    REQUIRE(chosen.Size() == 1u);
    CHECK(chosen[0] == EditorShortcut{ui::KeyCode::K, ui::KeyModifiers::Ctrl});
    CHECK(button->Chord() == chosen[0]);
    CHECK(button->Text.Value() == u8"Ctrl+K");

    // Escape cancels: the chord stays, nothing chosen.
    button->BeginCapture();
    ui::KeyEventArgs escape;
    escape.Key = ui::KeyCode::Escape;
    button->OnKeyDown(escape);
    CHECK_FALSE(button->IsCapturing());
    CHECK(chosen.Size() == 1u);
    CHECK(button->Text.Value() == u8"Ctrl+K");

    // Delete chooses "no shortcut".
    button->BeginCapture();
    ui::KeyEventArgs del;
    del.Key = ui::KeyCode::Delete;
    button->OnKeyDown(del);
    REQUIRE(chosen.Size() == 2u);
    CHECK_FALSE(chosen[1].IsSet());
    CHECK(button->Text.Value() == u8"(none)");

    // Losing focus cancels a capture in progress.
    button->BeginCapture();
    CHECK(button->IsCapturing());
    button->OnFocusLost();
    CHECK_FALSE(button->IsCapturing());
    CHECK(chosen.Size() == 2u);

    // SetChord shows a chord set from outside (Reset showing the default) and ends a capture.
    button->BeginCapture();
    button->SetChord(ctrlS);
    CHECK_FALSE(button->IsCapturing());
    CHECK(button->Text.Value() == u8"Ctrl+S");
}
