// SPDX-License-Identifier: MIT
// Copyright (c) 2026-Present Robert Campbell
// Editor::App tests - the page toolbar built from the action registry over ITS page: the
// standard set labelled from the declarations, a click executing over the toolbar's page even
// when another page is active, Refresh answering about this page, a domain action added by
// id as a button or a toggle, an unknown id refused, the standard set a page asks for, and the
// playback transport over a page that publishes IPlaybackPage.
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

    class PlayPage final : public EditorPage, public IPlaybackPage
    {
    public:
        PlayPage() : EditorPage(DefaultAllocator()) { Provide<IPlaybackPage>(*this); }
        [[nodiscard]] StringView Title() const override { return u8"play"; }
        [[nodiscard]] Status Save() override { return Status{}; }
        [[nodiscard]] bool CanPlay() const override { return loaded; }
        [[nodiscard]] bool IsPlaying() const override { return playing; }
        void Play() override { playing = true; }
        void Pause() override { playing = false; }
        void Stop() override
        {
            playing = false;
            position = 0;
        }
        void Restart() override
        {
            position = 0;
            playing = true;
        }
        bool loaded = true;
        bool playing = false;
        i32 position = 0;
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

TEST_CASE("page-toolbar: a page asks for its standard set")
{
    EditorContext context{DefaultAllocator()};
    DeclareStandardSet(context.Actions());
    auto* page = static_cast<RunPage*>(context.AdoptPage(
        UniquePtr<EditorPage>(DefaultAllocator().New<RunPage>(), DefaultAllocator())));
    auto text = MakeRef<app::PageToolbar>(DefaultAllocator(), *page, context.Actions(),
                                          app::PageToolbar::Standard::Save);
    CHECK(text->BoundCount() == 1u);
    CHECK(text->ButtonFor(u8"file.save") != nullptr);
    CHECK(text->ButtonFor(u8"page.discardChanges") == nullptr); // a text page's text is no stack
    auto none = MakeRef<app::PageToolbar>(DefaultAllocator(), *page, context.Actions(),
                                          app::PageToolbar::Standard::None);
    CHECK(none->BoundCount() == 0u);
    CHECK(none->ChildCount() == 0u);
}

TEST_CASE("page-toolbar: the playback transport drives its page")
{
    EditorContext context{DefaultAllocator()};
    app::RegisterPlaybackActions(context.Actions());
    auto* page = static_cast<PlayPage*>(context.AdoptPage(
        UniquePtr<EditorPage>(DefaultAllocator().New<PlayPage>(), DefaultAllocator())));
    auto toolbar = MakeRef<app::PageToolbar>(DefaultAllocator(), *page, context.Actions(),
                                             app::PageToolbar::Standard::None);
    toolbar->AddPlayback();
    CHECK(toolbar->BoundCount() == 3u);
    CHECK(toolbar->ChildCount() == 3u); // no leading separator on an empty bar
    auto* play = Cast<ui::toolkit::ToolbarToggle>(toolbar->ButtonFor(u8"playback.play"));
    REQUIRE(play != nullptr); // Play is a toggle

    // Play, then pause, through the one toggle; the check follows the page.
    toolbar->Refresh();
    CHECK(play->IsEnabled);
    CHECK_FALSE(play->IsChecked());
    play->OnClick.Invoke(play);
    CHECK(page->playing);
    toolbar->Refresh();
    CHECK(play->IsChecked());
    play->OnClick.Invoke(play);
    CHECK_FALSE(page->playing);

    // Stop rewinds; Restart plays from the start.
    page->playing = true;
    page->position = 7;
    ui::toolkit::ToolbarButton* stop = toolbar->ButtonFor(u8"playback.stop");
    stop->OnClick.Invoke(stop);
    CHECK_FALSE(page->playing);
    CHECK(page->position == 0);
    page->position = 3;
    ui::toolkit::ToolbarButton* restart = toolbar->ButtonFor(u8"playback.restart");
    restart->OnClick.Invoke(restart);
    CHECK(page->playing);
    CHECK(page->position == 0);

    // Nothing loaded: nothing enabled.
    page->loaded = false;
    toolbar->Refresh();
    CHECK_FALSE(play->IsEnabled);
    CHECK_FALSE(stop->IsEnabled);

    // A page that plays nothing has every playback action disabled.
    auto* plain = context.AdoptPage(
        UniquePtr<EditorPage>(DefaultAllocator().New<RunPage>(), DefaultAllocator()));
    CHECK_FALSE(context.Actions().IsEnabled(u8"playback.play", plain));
    CHECK_FALSE(context.Actions().IsEnabled(u8"playback.restart", plain));
}
