// SPDX-License-Identifier: MIT
// Copyright (c) 2026-Present Robert Campbell

// The command palette over a registry: the rows a filter yields and their order, the highlight
// under the arrow keys, Enter running the highlighted action over the active subject (a refused
// one keeps the palette open and says so), and the keys intercepted ahead of the filter box.

#include <doctest/doctest.h>
#include "Core/Prelude.h"

import foundation.core;
import foundation.ui;
import editor.core;
import editor.app;

using namespace foundation::core;
using namespace editor;
namespace ui = foundation::ui;

namespace
{
    EditorActionDeclaration Declare(StringView id, StringView label, StringView description,
                                    StringView menuPath)
    {
        EditorActionDeclaration d;
        d.id = String(id);
        d.label = String(label);
        d.description = String(description);
        d.menuPath = String(menuPath);
        d.execute = [](EditorPage*) {};
        return d;
    }
    String Labels(const app::CommandPaletteDialog& palette)
    {
        String out;
        for (const EditorActionDeclaration* row : palette.Rows())
        {
            if (!out.IsEmpty())
            {
                out += u8"|";
            }
            out += row->label.AsView();
        }
        return out;
    }
}

TEST_CASE("command-palette: the rows follow the filter - label starts first, label contains next, "
          "description or id last; the highlight moves clamped; Enter runs the highlighted action "
          "and a refused one keeps the palette open")
{
    EditorActionRegistry actions;
    u32 saves = 0;
    u32 resets = 0;
    bool dirty = false;
    EditorActionDeclaration save = Declare(u8"file.save", u8"Save", u8"Save the active page", u8"File/Save");
    save.shortcut = EditorShortcut{ui::KeyCode::S, ui::KeyModifiers::Ctrl};
    save.enabled = [&dirty](EditorPage*) { return dirty; };
    save.execute = [&saves](EditorPage*) { ++saves; };
    REQUIRE(actions.Register(Move(save)));
    REQUIRE(actions.Register(Declare(u8"file.saveAs", u8"Save As...", u8"Save the active page under a new name", u8"File/Save As...")));
    EditorActionDeclaration reset = Declare(u8"view.resetLayout", u8"Reset Layout",
                                            u8"Reset the panel layout to the default", u8"View/Reset Layout");
    reset.execute = [&resets](EditorPage*) { ++resets; };
    REQUIRE(actions.Register(Move(reset)));
    REQUIRE(actions.Register(Declare(u8"edit.undo", u8"Undo", u8"Take the last edit back", u8"Edit/Undo")));
    REQUIRE(actions.Register(Declare(u8"scene.entity.saveSelection", u8"Duplicate", u8"Copy the selection beside itself", u8"Scene/Entity/Duplicate")));

    auto palette = MakeRef<app::CommandPaletteDialog>(DefaultAllocator(), actions);
    // No filter: everything, in registration order, the first row highlighted.
    CHECK(Labels(*palette) == u8"Save|Save As...|Reset Layout|Undo|Duplicate");
    CHECK(palette->Highlighted() == 0);

    // "sav": the labels starting with it first, then the one whose id contains it (Duplicate's
    // id has "save"), never Undo or Reset Layout.
    palette->SetFilter(u8"sav");
    CHECK(Labels(*palette) == u8"Save|Save As...|Duplicate");
    CHECK(palette->Highlighted() == 0);
    // "layout": a label containing it (not at the start) and a description containing it.
    palette->SetFilter(u8"LAYOUT");
    CHECK(Labels(*palette) == u8"Reset Layout");
    // A description match alone.
    palette->SetFilter(u8"last edit");
    CHECK(Labels(*palette) == u8"Undo");
    // Nothing.
    palette->SetFilter(u8"zzz");
    CHECK(palette->Rows().IsEmpty());
    CHECK(palette->Highlighted() == -1);
    CHECK(palette->ExecuteHighlighted().Code() == ErrorCode::NotFound);

    // The highlight under the keys, clamped at both ends.
    palette->SetFilter(u8"");
    ui::KeyEventArgs down;
    down.Key = ui::KeyCode::Down;
    palette->OnKeyDownCapture(down);
    CHECK(down.Handled);
    CHECK(palette->Highlighted() == 1);
    palette->MoveHighlight(10);
    CHECK(palette->Highlighted() == 4);
    ui::KeyEventArgs up;
    up.Key = ui::KeyCode::Up;
    for (int i = 0; i < 9; ++i)
    {
        palette->OnKeyDownCapture(up);
    }
    CHECK(palette->Highlighted() == 0);
    // A letter is the filter box's, not the palette's.
    ui::KeyEventArgs letter;
    letter.Key = ui::KeyCode::A;
    palette->OnKeyDownCapture(letter);
    CHECK_FALSE(letter.Handled);

    // Enter on a disabled action refuses and keeps the palette (no close), saying so.
    palette->SetFilter(u8"sav");
    bool closed = false;
    palette->OnClosed.Add(ui::Event<void(ui::Dialog*, ui::DialogResult)>::Handler{
        [&closed](ui::Dialog*, ui::DialogResult) { closed = true; }});
    ui::KeyEventArgs enter;
    enter.Key = ui::KeyCode::Return;
    palette->OnKeyDownCapture(enter);
    CHECK(enter.Handled);
    CHECK(saves == 0u);
    CHECK_FALSE(closed);
    // Enabled: it runs and the palette closes with OK.
    dirty = true;
    CHECK(palette->ExecuteHighlighted().IsOk());
    CHECK(saves == 1u);
    CHECK(closed);
    CHECK(palette->Result == ui::DialogResult::OK);

    // Another palette runs a different row through the highlight.
    auto second = MakeRef<app::CommandPaletteDialog>(DefaultAllocator(), actions);
    second->SetFilter(u8"reset");
    CHECK(second->ExecuteHighlighted().IsOk());
    CHECK(resets == 1u);
}
