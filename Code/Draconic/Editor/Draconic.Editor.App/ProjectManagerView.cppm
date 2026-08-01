// Draconic::EditorApp - :project_manager_view partition.
//
// The PROJECT MANAGER screen (Godot-style, built into the single editor exe): shown at startup
// when no project was given on the command line, and returned to by File > Close Project. One
// full-window root view - recent projects (from the per-user registry section, most recent
// first, live-probed for display truth) plus Open/New/Remove actions.
//
// The view owns list presentation and registry MUTATION (remove); everything with project
// consequences goes through callbacks the application binds:
//   OnOpenProject(dir)         - open an existing project (the app runs the version prompt)
//   OnCreateProject(dir, name) - scaffold + open a new project at dir
//   OnStoreChanged()           - the registry section changed; persist the settings store
//
// Rows are probed on every Rebuild: a missing/unreadable manifest renders dim "(missing)",
// a newer-engine stamp renders amber - the list never lies about what Open will find.

module;
#include "Draconic.Core/Prelude.h"
#include "Draconic.Core/Reflection/Reflect.h"

export module draconic.editor.app:project_manager_view;

import draconic.core;
import draconic.settings;
import draconic.shell;
import draconic.ui;
import draconic.ui.toolkit;
import draconic.engine.project;
import draconic.editor.core;

using namespace draconic::core;

export namespace draconic::editor::app
{
    namespace ui = draconic::ui;
    namespace project = draconic::project;

    class ProjectManagerView final
    {
    public:
        Function<void(StringView)> OnOpenProject;
        Function<void(StringView, StringView)> OnCreateProject;
        Function<void()> OnStoreChanged;

        ProjectManagerView() = default;
        ProjectManagerView(const ProjectManagerView&) = delete;
        ProjectManagerView& operator=(const ProjectManagerView&) = delete;

        void Build(draconic::editor::ProjectManagerController& controller,
                   shell::IDialogService* dialogs, ui::UIContext* uiContext, u32 width,
                   u32 height)
        {
            m_controller = &controller;
            m_dialogs = dialogs;
            m_uiContext = uiContext;
            m_root = MakeRef<ui::RootView>(DefaultAllocator());
            m_root->ViewportSize = Float2{static_cast<f32>(width), static_cast<f32>(height)};
            m_root->DpiScale = 1.0f;

            auto column = MakeRef<ui::FlexLayout>(DefaultAllocator());
            column->Direction = ui::Orientation::Vertical;
            column->Spacing = 10;
            column->Padding = ui::Thickness{24, 20};

            // Header: product + engine version (the manager IS the version disambiguator).
            {
                auto header = MakeRef<ui::FlexLayout>(DefaultAllocator());
                header->Direction = ui::Orientation::Horizontal;
                header->Spacing = 10;
                auto title = MakeRef<ui::Label>(DefaultAllocator());
                title->SetText(u8"Draconic Editor");
                title->FontSize.SetValue(22.0f);
                header->AddView(title.Get());
                auto version = MakeRef<ui::Label>(DefaultAllocator());
                String v(u8"engine ");
                v += project::kEngineVersionString;
                version->SetText(v.AsView());
                version->FontSize.SetValue(12.0f);
                version->TextColor.SetValue(Optional<Color>(Color{0.6f, 0.6f, 0.6f, 1.0f}));
                header->AddView(version.Get());
                column->AddView(header.Get());
            }

            {
                auto caption = MakeRef<ui::Label>(DefaultAllocator());
                caption->SetText(u8"Recent projects");
                caption->FontSize.SetValue(13.0f);
                caption->TextColor.SetValue(Optional<Color>(Color{0.75f, 0.75f, 0.75f, 1.0f}));
                column->AddView(caption.Get());
            }

            m_adapter = MakeUnique<RowAdapter>(DefaultAllocator(), *this);
            m_list = MakeRef<ui::ListView>(DefaultAllocator());
            m_list->ItemHeight.SetValue(26.0f);
            m_list->SetAdapter(m_adapter.Get());
            {
                ProjectManagerView* self = this;
                m_list->OnItemClicked.Add(
                    [self](i32 position, i32 clickCount, f32, f32)
                    {
                        if (clickCount >= 2)
                        {
                            self->OpenAt(position);
                        }
                    });
                auto lp = MakeRef<ui::FlexLayoutParams>(DefaultAllocator());
                lp->Width = ui::SizeSpec::Match();
                lp->Grow = 1.0f;
                column->AddView(m_list.Get(), lp);
            }

            // Action row.
            {
                auto actions = MakeRef<ui::FlexLayout>(DefaultAllocator());
                actions->Direction = ui::Orientation::Horizontal;
                actions->Spacing = 8;
                ProjectManagerView* self = this;

                auto open = MakeRef<ui::Button>(DefaultAllocator(), StringView(u8"Open"));
                open->OnClick.Add([self](ui::ButtonBase*)
                                  { self->OpenAt(self->m_list->Selection.FirstSelected()); });
                actions->AddView(open.Get());

                auto browse = MakeRef<ui::Button>(DefaultAllocator(), StringView(u8"Open Folder..."));
                browse->OnClick.Add([self](ui::ButtonBase*) { self->BrowseAndOpen(); });
                actions->AddView(browse.Get());

                auto create = MakeRef<ui::Button>(DefaultAllocator(), StringView(u8"New Project..."));
                create->OnClick.Add([self](ui::ButtonBase*) { self->ShowCreateDialog(); });
                actions->AddView(create.Get());

                auto remove = MakeRef<ui::Button>(DefaultAllocator(), StringView(u8"Remove From List"));
                remove->OnClick.Add([self](ui::ButtonBase*)
                                    { self->RemoveAt(self->m_list->Selection.FirstSelected()); });
                actions->AddView(remove.Get());

                column->AddView(actions.Get());
            }

            m_status = MakeRef<ui::Label>(DefaultAllocator());
            m_status->FontSize.SetValue(12.0f);
            m_status->TextColor.SetValue(Optional<Color>(Color{0.7f, 0.7f, 0.7f, 1.0f}));
            column->AddView(m_status.Get());

            m_root->AddView(column.Get());
            Rebuild();
        }

        [[nodiscard]] ui::RootView* Root() const noexcept { return m_root.Get(); }

        void SetStatus(StringView text) { m_status->SetText(text); }

        // Re-read the registry + live-probe every row (display truth: missing dirs, live
        // name/version). Call after any registry mutation and on every return to the manager.
        void Rebuild()
        {
            m_rows.Clear();
            const RecentProjectsSettings& reg = m_controller->Entries();
            for (const RecentProjectEntry& entry : reg.entries)
            {
                Row row;
                row.path = entry.path;
                project::ProjectSettings probed;
                if (ProbeProject(entry.path.AsView(), probed).IsOk())
                {
                    row.name = probed.name;
                    row.engineVersion = probed.engineVersion;
                    row.relation = CompareProjectEngineVersion(probed.engineVersion.AsView());
                }
                else
                {
                    row.name = entry.name; // last-known snapshot, flagged missing
                    row.engineVersion = entry.engineVersion;
                    row.missing = true;
                }
                m_rows.PushBack(Move(row));
            }
            if (m_list)
            {
                m_list->NotifyDataChanged();
            }
        }

    private:
        struct Row
        {
            String path;
            String name;
            String engineVersion;
            EngineVersionRelation relation = EngineVersionRelation::Same;
            bool missing = false;
        };

        class RowAdapter final : public ui::ListAdapterBase
        {
        public:
            explicit RowAdapter(ProjectManagerView& owner) : m_owner(&owner) {}
            [[nodiscard]] i32 ItemCount() const override
            {
                return static_cast<i32>(m_owner->m_rows.Size());
            }
            [[nodiscard]] RefPtr<ui::View> CreateView(i32) override
            {
                auto row = MakeRef<ui::FlexLayout>(DefaultAllocator());
                row->Direction = ui::Orientation::Horizontal;
                row->Spacing = 8;
                row->Padding = ui::Thickness{6, 3};
                auto name = MakeRef<ui::Label>(DefaultAllocator());
                name->FontSize.SetValue(13.0f);
                row->AddView(name.Get());
                auto path = MakeRef<ui::Label>(DefaultAllocator());
                path->FontSize.SetValue(12.0f);
                auto grow = MakeRef<ui::FlexLayoutParams>(DefaultAllocator());
                grow->Grow = 1.0f;
                row->AddView(path.Get(), grow);
                return RefPtr<ui::View>(row.Get());
            }
            void BindView(ui::View* view, i32 position) override
            {
                auto* row = Cast<ui::FlexLayout>(view);
                if (row == nullptr || row->ChildCount() < 2 || position < 0 ||
                    position >= static_cast<i32>(m_owner->m_rows.Size()))
                {
                    return;
                }
                const Row& r = m_owner->m_rows[static_cast<usize>(position)];
                auto* name = Cast<ui::Label>(row->GetChildAt(0));
                auto* path = Cast<ui::Label>(row->GetChildAt(1));
                String text = r.name.IsEmpty() ? String(u8"(unnamed)") : r.name;
                if (!r.engineVersion.IsEmpty())
                {
                    text += u8"  -  ";
                    text += r.engineVersion;
                }
                if (r.missing)
                {
                    text += u8"  (missing)";
                }
                else if (r.relation == EngineVersionRelation::ProjectNewer)
                {
                    text += u8"  (newer engine!)";
                }
                name->SetText(text.AsView());
                path->SetText(r.path.AsView());
                const Color nameColor =
                    r.missing ? Color{0.5f, 0.5f, 0.5f, 1.0f}
                    : (r.relation == EngineVersionRelation::ProjectNewer)
                        ? Color{0.95f, 0.75f, 0.3f, 1.0f}
                        : Color{0.9f, 0.9f, 0.9f, 1.0f};
                name->TextColor.SetValue(Optional<Color>(nameColor));
                path->TextColor.SetValue(Optional<Color>(Color{0.55f, 0.55f, 0.55f, 1.0f}));
            }

        private:
            ProjectManagerView* m_owner;
        };

        void OpenAt(i32 position)
        {
            if (position < 0 || position >= static_cast<i32>(m_rows.Size()))
            {
                SetStatus(u8"Select a project first.");
                return;
            }
            const Row& row = m_rows[static_cast<usize>(position)];
            if (row.missing)
            {
                SetStatus(u8"That project is missing (no Project.xml at its path).");
                return;
            }
            if (OnOpenProject)
            {
                OnOpenProject(row.path.AsView());
            }
        }

        void RemoveAt(i32 position)
        {
            if (position < 0 || position >= static_cast<i32>(m_rows.Size()))
            {
                SetStatus(u8"Select a project first.");
                return;
            }
            const String path = m_rows[static_cast<usize>(position)].path; // copy: Rebuild frees it
            if (m_controller->Remove(path.AsView()))
            {
                Rebuild();
                if (OnStoreChanged)
                {
                    OnStoreChanged();
                }
            }
        }

        void BrowseAndOpen()
        {
            if (m_dialogs == nullptr)
            {
                return;
            }
            ProjectManagerView* self = this;
            m_dialogs->ShowOpenFolder(draconic::shell::DialogResultCallback{
                [self](Span<const String> paths)
                {
                    if (paths.Size() == 0)
                    {
                        return; // cancelled
                    }
                    project::ProjectSettings probed;
                    if (!ProbeProject(paths[0].AsView(), probed).IsOk())
                    {
                        self->SetStatus(
                            u8"Not a project (no Project.xml there). Use New Project to start "
                            u8"one.");
                        return;
                    }
                    if (self->OnOpenProject)
                    {
                        self->OnOpenProject(paths[0].AsView());
                    }
                }});
        }

        void ShowCreateDialog()
        {
            RefPtr<ui::Dialog> dialog =
                MakeRef<ui::Dialog>(DefaultAllocator(), StringView(u8"New Project"));
            auto column = MakeRef<ui::FlexLayout>(DefaultAllocator());
            column->Direction = ui::Orientation::Vertical;
            column->Spacing = 6;

            auto nameEdit = MakeRef<ui::EditText>(DefaultAllocator());
            nameEdit->SetPlaceholder(u8"Project name");
            {
                auto lp = MakeRef<ui::FlexLayoutParams>(DefaultAllocator());
                lp->Width = ui::SizeSpec::Match();
                column->AddView(nameEdit.Get(), lp);
            }

            auto dirRow = MakeRef<ui::FlexLayout>(DefaultAllocator());
            dirRow->Direction = ui::Orientation::Horizontal;
            dirRow->Spacing = 6;
            auto dirEdit = MakeRef<ui::EditText>(DefaultAllocator());
            dirEdit->SetPlaceholder(u8"Parent directory (the project is created inside it)");
            {
                auto lp = MakeRef<ui::FlexLayoutParams>(DefaultAllocator());
                lp->Grow = 1.0f;
                dirRow->AddView(dirEdit.Get(), lp);
            }
            auto browse = MakeRef<ui::Button>(DefaultAllocator(), StringView(u8"Browse..."));
            {
                ProjectManagerView* self = this;
                ui::EditText* dirRaw = dirEdit.Get();
                browse->OnClick.Add(
                    [self, dirRaw](ui::ButtonBase*)
                    {
                        if (self->m_dialogs == nullptr)
                        {
                            return;
                        }
                        self->m_dialogs->ShowOpenFolder(draconic::shell::DialogResultCallback{
                            [dirRaw](Span<const String> paths)
                            {
                                if (paths.Size() > 0)
                                {
                                    dirRaw->SetText(paths[0].AsView());
                                }
                            }});
                    });
            }
            dirRow->AddView(browse.Get());
            {
                auto lp = MakeRef<ui::FlexLayoutParams>(DefaultAllocator());
                lp->Width = ui::SizeSpec::Match();
                column->AddView(dirRow.Get(), lp);
            }
            dialog->SetContent(column.Get());
            dialog->MinWidth.SetValue(460.0f);

            ui::Dialog* rawDialog = dialog.Get();
            ui::EditText* nameRaw = nameEdit.Get();
            ui::EditText* dirRaw = dirEdit.Get();
            ProjectManagerView* self = this;
            ui::Button* create = dialog->AddButton(u8"Create", ui::DialogResult::None);
            create->OnClick.Add(
                [self, rawDialog, nameRaw, dirRaw](ui::ButtonBase*)
                {
                    const StringView name = nameRaw->Text();
                    const StringView parent = dirRaw->Text();
                    if (name.IsEmpty() || parent.IsEmpty())
                    {
                        return; // both fields required; dialog stays up
                    }
                    rawDialog->Close(ui::DialogResult::OK);
                    if (self->OnCreateProject)
                    {
                        self->OnCreateProject(PathJoin(parent, name).AsView(), name);
                    }
                });
            dialog->AddButton(u8"Cancel", ui::DialogResult::Cancel);
            dialog->Show(m_uiContext);
        }

        draconic::editor::ProjectManagerController* m_controller = nullptr;
        shell::IDialogService* m_dialogs = nullptr;
        ui::UIContext* m_uiContext = nullptr;
        RefPtr<ui::RootView> m_root;
        RefPtr<ui::ListView> m_list;
        RefPtr<ui::Label> m_status;
        UniquePtr<RowAdapter> m_adapter;
        Array<Row> m_rows;
    };
}
