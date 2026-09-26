// SPDX-License-Identifier: MIT
// Copyright (c) 2026-Present Robert Campbell
// Editor::App tests - the page toolbar built from the action registry over ITS page: the
// standard set labelled from the declarations, a click executing over the toolbar's page even
// when another page is active, Refresh answering about this page, a domain action added by
// id as a button or a toggle, and an unknown id refused.
#include <doctest/doctest.h>
#include "Core/Prelude.h"

import foundation.core;
import foundation.ui;
import foundation.ui.toolkit;
import editor.core;
import editor.app;

using namespace foundation::core;
using namespace editor;
namespace ui = foundation::ui;

namespace
{
    class IRunnable : public IPageService
    {
    public:
        virtual void Toggle() = 0;
        [[nodiscard]] virtual bool Running() const = 0;
    };
    class RunPage final : public EditorPage, public IRunnable
    {
    public:
        RunPage() : EditorPage(DefaultAllocator()) { Provide<IRunnable>(*this); }
        [[nodiscard]] StringView Title() const override { return u8"run"; }
        [[nodiscard]] Status Save() override
        {
            ++saves;
            ClearDirty();
            return Status{};
        }
        void Toggle() override { running = !running; }
        [[nodiscard]] bool Running() const override { return running; }
        u32 saves = 0;
        bool running = false;
    };

    // The standard set as the application declares it, over the subject page.
    void DeclareStandardSet(EditorActionRegistry& actions)
    {
        EditorActionDeclaration save;
        save.id = String(u8"file.save");
        save.label = String(u8"Save");
        save.enabled = [](EditorPage* page) { return page != nullptr && page->IsDirty(); };
        save.execute = [](EditorPage* page) { (void)page->Save(); };
        REQUIRE(actions.Register(Move(save)));
        EditorActionDeclaration undo;
        undo.id = String(u8"edit.undo");
        undo.label = String(u8"Undo");
        undo.enabled = [](EditorPage* page) { return page != nullptr && page->Commands().CanUndo(); };
        undo.execute = [](EditorPage* page) { page->Commands().Undo(); };
        REQUIRE(actions.Register(Move(undo)));
        EditorActionDeclaration redo;
        redo.id = String(u8"edit.redo");
        redo.label = String(u8"Redo");
        redo.enabled = [](EditorPage* page) { return page != nullptr && page->Commands().CanRedo(); };
        redo.execute = [](EditorPage* page) { page->Commands().Redo(); };
        REQUIRE(actions.Register(Move(redo)));
        EditorActionDeclaration discard;
        discard.id = String(u8"page.discardChanges");
        discard.label = String(u8"Discard Changes");
        discard.enabled = [](EditorPage* page) { return page != nullptr && page->IsDirty(); };
        discard.execute = [](EditorPage* page) { page->DiscardChanges(); };
        REQUIRE(actions.Register(Move(discard)));
    }
}

TEST_CASE("page-toolbar: built from the registry over its own page - labels from the "
          "declarations, clicks over THIS page while another is active, Refresh about this page, "
          "a domain toggle by id, an unknown id refused")
{
    EditorContext context{DefaultAllocator()};
    DeclareStandardSet(context.Actions());
    EditorActionDeclaration run;
    run.id = String(u8"run.toggle");
    run.label = String(u8"Run");
    run.kind = EditorActionKind::Toggle;
    run.enabled = [](EditorPage* page) { return ServiceOf<IRunnable>(page) != nullptr; };
    run.checked = [](EditorPage* page)
    {
        IRunnable* runnable = ServiceOf<IRunnable>(page);
        return runnable != nullptr && runnable->Running();
    };
    run.execute = [](EditorPage* page) { ServiceOf<IRunnable>(page)->Toggle(); };
    REQUIRE(context.Actions().Register(Move(run)));

    auto* mine = static_cast<RunPage*>(context.AdoptPage(
        UniquePtr<EditorPage>(DefaultAllocator().New<RunPage>(), DefaultAllocator())));
    auto* other = static_cast<RunPage*>(context.AdoptPage(
        UniquePtr<EditorPage>(DefaultAllocator().New<RunPage>(), DefaultAllocator())));
    REQUIRE(context.ActivePage() == other); // the last adopted is active; the toolbar is mine's

    auto toolbar = MakeRef<app::PageToolbar>(DefaultAllocator(), *mine, context.Actions());
    CHECK(toolbar->BoundCount() == 4u);
    ui::toolkit::ToolbarButton* runButton = toolbar->AddAction(u8"run.toggle");
    REQUIRE(runButton != nullptr);
    CHECK(toolbar->BoundCount() == 5u);
    CHECK(toolbar->AddAction(u8"nobody.home") == nullptr);
    CHECK(toolbar->BoundCount() == 5u);
    CHECK(runButton->Text() == u8"Run");

    ui::toolkit::ToolbarButton* saveButton = toolbar->ButtonFor(u8"file.save");
    REQUIRE(saveButton != nullptr);
    CHECK(saveButton->Text() == u8"Save");
    CHECK(toolbar->ButtonFor(u8"nobody.home") == nullptr);

    // Refresh answers about MINE: clean, nothing to undo - only the toggle is enabled.
    toolbar->Refresh();
    CHECK_FALSE(saveButton->IsEnabled);
    CHECK_FALSE(toolbar->ButtonFor(u8"edit.undo")->IsEnabled);
    CHECK(runButton->IsEnabled);
    auto* runToggle = Cast<ui::toolkit::ToolbarToggle>(runButton);
    REQUIRE(runToggle != nullptr);
    CHECK_FALSE(runToggle->IsChecked());

    // A click executes over MINE, not the active page.
    runButton->OnClick.Invoke(runButton);
    CHECK(mine->running);
    CHECK_FALSE(other->running);
    toolbar->Refresh();
    CHECK(runToggle->IsChecked());

    // Save enabled follows MINE's dirty state, whatever the active page does.
    other->MarkDirty();
    toolbar->Refresh();
    CHECK_FALSE(saveButton->IsEnabled); // file.save over mine: clean
    mine->MarkDirty();
    toolbar->Refresh();
    CHECK(saveButton->IsEnabled);
    saveButton->OnClick.Invoke(saveButton);
    CHECK(mine->saves == 1u);
    CHECK(other->saves == 0u);
    CHECK(other->IsDirty()); // untouched

    context.ClosePage(other);
    context.ClosePage(mine);
}
