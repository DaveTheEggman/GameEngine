// SPDX-License-Identifier: MIT
// Copyright (c) 2026-Present Robert Campbell

// The game-UI editor's headless halves: the markup probe both pages show in their code
// editors, the stock preview markup, and the linked-source read without a project. Ported
// back from the Beef port's Editor.GameUI.Tests (2026-09-20).
#include <doctest/doctest.h>
#include "Core/Prelude.h"

import foundation.core;
import foundation.ui;
import foundation.ui.toolkit;
import editor.core;
import editor.gameui;

using namespace foundation::core;
namespace toolkit = foundation::ui::toolkit;

TEST_CASE("gameui editor: the probe reports one error on the failing line, and none when clean")
{
    Array<toolkit::CodeDiagnostic> diagnostics;
    CHECK(editor::markup_diagnostics::Probe(DefaultAllocator(),
                                            u8"<Panel>\n  <Label text=\"a\"/>\n</Panel>", diagnostics));
    CHECK(diagnostics.IsEmpty());

    CHECK_FALSE(editor::markup_diagnostics::Probe(
        DefaultAllocator(), u8"<Panel>\n  <Label text=\"a\">\n</Panel>", diagnostics));
    REQUIRE(diagnostics.Size() == 1u);
    CHECK(diagnostics[0].isError);
    CHECK(diagnostics[0].line >= 1); // past the first line: the parser named a real line
    CHECK_FALSE(diagnostics[0].message.IsEmpty());
}

TEST_CASE("gameui editor: through an editor, the margin takes the diagnostic and drops it when clean")
{
    auto editorRef = MakeRef<toolkit::CodeEditView>(DefaultAllocator());
    toolkit::CodeEditView& editor = *editorRef;
    editor.SetText(u8"<a><b></a>");
    CHECK_FALSE(editor::markup_diagnostics::Apply(DefaultAllocator(), u8"<a><b></a>", editor));
    CHECK(editor.Document().Diagnostics().Size() == 1u);
    CHECK(editor::markup_diagnostics::Apply(DefaultAllocator(), u8"<a/>", editor));
    CHECK(editor.Document().Diagnostics().Size() == 0u);

    // The stock preview a new theme previews against is itself clean markup.
    CHECK(editor::kStockPreviewMarkup.StartsWith(u8"<Panel"));
    Array<toolkit::CodeDiagnostic> diagnostics;
    CHECK(editor::markup_diagnostics::Probe(DefaultAllocator(), editor::kStockPreviewMarkup, diagnostics));
}

TEST_CASE("gameui editor: a linked source reads empty without a project")
{
    editor::EditorContext context{DefaultAllocator()};
    const String text = editor::ReadProjectSource(context, u8"UI/missing.sml");
    CHECK(text.IsEmpty());
}
