// Draconic::EditorApp - :assets_view partition.
//
// AssetsView: the Assets panel (asset-pipeline design §7) - source-DB-backed, the typed DB is
// the truth (Traktor's DatabaseView model, not a directory scan). Left = group tree; right =
// [breadcrumb | list/grid toggle] over [filter] over the selected group's content. The content
// area shows SUBGROUPS first (double-click descends; the breadcrumb climbs back up), then
// instances; a non-empty filter searches instances across ALL groups. Rows show the name
// (gold when favorited) with a trailing "Type [badge]" meta label - the badge is the cook
// state (cooked/missing/FAILED; builder-less types like scenes show none). Interactions:
//   - double-click: descend into a group / open the instance's editor page
//   - INLINE RENAME everywhere, same mechanics as the scene hierarchy (Sedulous browser
//     parity): slow-click the name / F2 / menu Rename edit in place - instances, subgroup
//     rows, and the group tree alike; double-click stays navigation (never starts an edit)
//   - right-click row: Open / Rename / Duplicate / Cook / Rebuild / Delete (multi-select
//     aware: Ctrl/Shift extend the selection; Delete acts on every selected instance);
//     group rows and tree groups get Open / Rename / Delete Group (recursive, confirmed)
//   - right-click background (list, grid, or the group TREE): New <creator>... / Cook All /
//     Rebuild All
//   - Delete always confirms through a dialog, closes any open page editing the instance
//     first (via OnCloseInstancePage), and logs what was removed.
// The view refreshes off three revisions: source-DB shape (tracked locally via Rebuild calls),
// the cook service's revision (badges after a cook), and the filter text.

module;
#include "Core/Prelude.h"
#include "Core/Reflection/Reflect.h"
#include "Core/Log/Log.h"

export module draconic.editor.app:assets_view;

import draconic.core;
import draconic.content;
import draconic.fonts;
import draconic.ui;
import draconic.ui.toolkit;
import draconic.editor.core;
import :editor_icons;
import :import_dialog;

using namespace draconic::core;

export namespace draconic::editor::app
{
    namespace ui = draconic::ui;
    namespace tk = draconic::ui::toolkit;
    namespace content = draconic::content;

    class AssetsView final : public ui::ViewGroup
    {
        DRACONIC_OBJECT(AssetsView, ui::ViewGroup)
    public:
        /// Open an instance's editor page (wired by the application).
        Function<void(content::Instance&)> OnOpenInstance;
        /// Create an asset via a registry creator (wired by the application - it also opens it).
        /// `group` = the group the menu was invoked for (creations land there).
        Function<void(const draconic::editor::EditorContext::AssetCreator&, content::Group*)> OnCreate;
        /// Close any open editor page for this instance BEFORE it is deleted (wired by the
        /// application; called from a mutation-queue action, so synchronous teardown is safe).
        Function<void(const Guid&)> OnCloseInstancePage;

        AssetsView(draconic::editor::EditorContext& context, draconic::editor::EditorCookService& cook)
            : m_context(&context), m_cook(&cook)
        {
            auto split = MakeRef<tk::SplitView>(DefaultAllocator());
            split->SetSplitRatio(0.3f);

            // Left: the group tree.
            m_treeAdapter = MakeUnique<TreeAdapter>(DefaultAllocator(), *this);
            m_tree = MakeRef<ui::TreeView>(DefaultAllocator());
            m_tree->SetItemHeight(22.0f);
            m_tree->SetAdapter(m_treeAdapter.Get());
            {
                AssetsView* self = this;
                m_tree->OnItemClick.Add([self](ui::TreeView::ItemClickInfo info) {
                    if (info.NodeId >= 0 && info.NodeId < static_cast<i32>(self->m_groups.Size()))
                    {
                        self->SelectGroup(self->m_groups[static_cast<usize>(info.NodeId)].group);
                    }
                });
                m_tree->OnItemRightClick.Add([self](i32 nodeId, f32 x, f32 y) {
                    if (nodeId >= 0 && nodeId < static_cast<i32>(self->m_groups.Size()))
                    {
                        self->SelectGroup(self->m_groups[static_cast<usize>(nodeId)].group);
                    }
                    self->ShowBackgroundMenu(self->m_tree.Get(), x, y);
                });
                m_tree->OnItemKeyDown.Add([self](i32 nodeId, ui::KeyEventArgs& e) {
                    if (nodeId < 0 || nodeId >= static_cast<i32>(self->m_groups.Size())) { return; }
                    content::Group* group = self->m_groups[static_cast<usize>(nodeId)].group;
                    if (group->Parent() == nullptr) { return; }   // the root: no rename/delete
                    if (e.Key == ui::KeyCode::F2)
                    {
                        self->StartRenameGroupInTree(group);
                        e.Handled = true;
                    }
                    else if (e.Key == ui::KeyCode::Delete)
                    {
                        self->ConfirmDeleteGroup(group);
                        e.Handled = true;
                    }
                });
            }

            // Right: [breadcrumb | view toggle] over [filter] over [list | grid].
            auto right = MakeRef<ui::FlexLayout>(DefaultAllocator());
            right->Direction = ui::Orientation::Vertical;
            right->Padding = ui::Thickness{ 6, 4 };   // inset the content off the edge (like the group tree)
            right->Spacing = 4;                        // separate the header row, filter, and content
            {
                AssetsView* self = this;

                auto header = MakeRef<ui::FlexLayout>(DefaultAllocator());
                header->Direction = ui::Orientation::Horizontal;
                header->Spacing = 4;
                m_breadcrumb = MakeRef<tk::BreadcrumbBar>(DefaultAllocator());
                m_breadcrumb->OnSegmentClicked.Add(
                    ui::Event<void(tk::BreadcrumbBar*, i32)>::Handler{ [self](tk::BreadcrumbBar*, i32 segment) {
                        self->NavigateToBreadcrumb(segment);
                    } });
                {
                    auto grow = MakeRef<ui::FlexLayoutParams>(DefaultAllocator());
                    grow->Grow = 1.0f;
                    grow->Height = ui::SizeSpec::Match();
                    header->AddView(m_breadcrumb.Get(), grow);
                }
                m_listToggle = MakeRef<ui::ToggleButton>(DefaultAllocator(), StringView(u8"List"));
                m_gridToggle = MakeRef<ui::ToggleButton>(DefaultAllocator(), StringView(u8"Grid"));
                m_listToggle->IsChecked.SetValue(true);
                m_listToggle->OnClick.Add([self](ui::ButtonBase*) { self->SetGridMode(false); });
                m_gridToggle->OnClick.Add([self](ui::ButtonBase*) { self->SetGridMode(true); });
                header->AddView(m_listToggle.Get());
                header->AddView(m_gridToggle.Get());
                {
                    auto lp = MakeRef<ui::FlexLayoutParams>(DefaultAllocator());
                    lp->Width = ui::SizeSpec::Match();
                    lp->Height = ui::SizeSpec::Fixed(ui::Unit::Px(26));
                    right->AddView(header.Get(), lp);
                }
            }
            m_filterEdit = MakeRef<ui::EditText>(DefaultAllocator());
            m_filterEdit->SetPlaceholder(u8"Filter all assets...");
            {
                AssetsView* self = this;
                m_filterEdit->OnTextChanged.Add([self](ui::EditText* edit) {
                    self->m_filter = String(edit->Text());
                    self->RebuildList();
                });
                auto lp = MakeRef<ui::FlexLayoutParams>(DefaultAllocator());
                lp->Width = ui::SizeSpec::Match();
                right->AddView(m_filterEdit.Get(), lp);
            }
            m_listAdapter = MakeUnique<ListAdapter>(DefaultAllocator(), *this);
            m_gridAdapter = MakeUnique<GridAdapter>(DefaultAllocator(), *this);
            m_list = MakeRef<ui::ListView>(DefaultAllocator());
            m_list->ItemHeight.SetValue(22.0f);
            m_list->Selection.Mode = ui::SelectionMode::Multiple;
            m_list->SetAdapter(m_listAdapter.Get());
            m_grid = MakeRef<ui::GridView>(DefaultAllocator());
            m_grid->CellWidth.SetValue(96.0f);
            m_grid->CellHeight.SetValue(84.0f);
            m_grid->Selection.Mode = ui::SelectionMode::Multiple;
            m_grid->Visibility = ui::VisibilityValue::Gone;
            m_grid->SetAdapter(m_gridAdapter.Get());
            {
                AssetsView* self = this;
                m_list->OnItemClicked.Add([self](i32 position, i32 clickCount, f32, f32) {
                    self->ActivateRow(position, clickCount);
                });
                m_list->OnItemRightClicked.Add([self](i32 position, f32 x, f32 y) {
                    self->ShowRowMenu(self->m_list.Get(), &self->m_list->Selection, position, x, y);
                });
                m_list->OnBackgroundRightClicked.Add([self](f32 x, f32 y) {
                    self->ShowBackgroundMenu(self->m_list.Get(), x, y);
                });
                m_grid->OnItemClicked.Add([self](i32 position, i32 clickCount, f32, f32) {
                    self->ActivateRow(position, clickCount);
                });
                m_grid->OnItemRightClicked.Add([self](i32 position, f32 x, f32 y) {
                    self->ShowRowMenu(self->m_grid.Get(), &self->m_grid->Selection, position, x, y);
                });
                m_grid->OnBackgroundRightClicked.Add([self](f32 x, f32 y) {
                    self->ShowBackgroundMenu(self->m_grid.Get(), x, y);
                });
                m_list->OnItemKeyDown.Add([self](i32 position, ui::KeyEventArgs& e) {
                    self->OnRowKeyDown(&self->m_list->Selection, position, e);
                });
                m_grid->OnItemKeyDown.Add([self](i32 position, ui::KeyEventArgs& e) {
                    self->OnRowKeyDown(&self->m_grid->Selection, position, e);
                });
                auto grow = MakeRef<ui::FlexLayoutParams>(DefaultAllocator());
                grow->Grow = 1.0f;
                grow->Width = ui::SizeSpec::Match();
                right->AddView(m_list.Get(), grow);
                auto grow2 = MakeRef<ui::FlexLayoutParams>(DefaultAllocator());
                grow2->Grow = 1.0f;
                grow2->Width = ui::SizeSpec::Match();
                right->AddView(m_grid.Get(), grow2);
            }

            split->SetPanes(m_tree.Get(), right.Get());
            AddView(split.Get());
            Rebuild();
        }

        ~AssetsView() override
        {
            m_tree->SetAdapter(nullptr);
            m_list->SetAdapter(nullptr);
            m_grid->SetAdapter(nullptr);
        }

        /// Per-frame: refresh badges after a cook finishes (DB shape changes call Rebuild()).
        void Refresh()
        {
            if (m_cook->Revision() != m_cookRevision)
            {
                m_cookRevision = m_cook->Revision();
                RebuildList();
            }
        }

        /// Import an OS file (drag-dropped onto the editor) into the selected group via the
        /// registered importers. An importer with options gets the pre-import dialog first;
        /// the actual import runs in ExecuteImport.
        void ImportFile(StringView path)
        {
            if (m_context->Project() == nullptr || Context == nullptr) { return; }
            const String ext = draconic::editor::FileExtensionLower(path);
            draconic::editor::IFileImporter* importer = m_context->Importers().FindFor(ext.AsView());
            if (importer == nullptr)
            {
                String message(u8"No importer for '");
                message += draconic::editor::FileNameOf(path);
                message += u8"'.";
                m_context->Notify(draconic::editor::NoticeKind::Warning, message.AsView());
                return;
            }

            RefPtr<draconic::editor::ImportOptions> options = importer->CreateOptions();
            if (options.Get() == nullptr)
            {
                ExecuteImport(String(path), importer, {});
                return;
            }
            content::Group* group = (m_selectedGroup != nullptr)
                ? m_selectedGroup : m_context->Project()->SourceDb().RootGroup();
            auto dialog = MakeRef<ImportOptionsDialog>(DefaultAllocator(), path,
                                                       group->Path().AsView(), options);
            AssetsView* self = this;
            dialog->OnImport = [self, file = String(path), importer,
                                opts = RefPtr<draconic::editor::ImportOptions>(options.Get())]() {
                self->ExecuteImport(file, importer, opts);
            };
            dialog->Show(Context);
        }

        /// Runs the import (post-dialog). Re-checks the cook build-lock HERE, so a dialog
        /// that sat open across a cook start still queues instead of racing the planner.
        void ExecuteImport(String path, draconic::editor::IFileImporter* importer,
                           RefPtr<draconic::editor::ImportOptions> options)
        {
            if (m_context->Project() == nullptr) { return; }
            // A cook in flight reads instance pointers snapshotted at plan time - creating
            // instances now is a race. Queue the import; the cook service replays it when idle.
            if (m_cook->MutationLocked())
            {
                AssetsView* self = this;
                m_cook->RunWhenIdle(Function<void()>{ [self, path, importer, options]() {
                    self->ExecuteImport(path, importer, options);
                } });
                m_context->Notify(draconic::editor::NoticeKind::Info,
                                  u8"Import queued until the current cook finishes.");
                return;
            }
            content::Group* group = (m_selectedGroup != nullptr)
                ? m_selectedGroup : m_context->Project()->SourceDb().RootGroup();
            Result<content::Instance*> imported =
                importer->Import(path.AsView(), *m_context->Project(), *group, options.Get());
            if (imported.HasValue() && imported.Value() != nullptr)
            {
                String message(u8"Imported '");
                message += imported.Value()->Name();
                message += u8"' (";
                message += importer->Label();
                message += u8").";
                m_context->Notify(draconic::editor::NoticeKind::Success, message.AsView());
                m_context->NotifyImported(*imported.Value(), options.Get());
            }
            else
            {
                String message(u8"Import failed: '");
                message += draconic::editor::FileNameOf(path.AsView());
                message += u8"' (see Console).";
                m_context->Notify(draconic::editor::NoticeKind::Error, message.AsView());
            }
            Rebuild();
        }

        /// Full rebuild: group tree + list (project open/close, create/delete/import).
        void Rebuild()
        {
            m_groups.Clear();
            content::Group* root = (m_context->Project() != nullptr)
                ? m_context->Project()->SourceDb().RootGroup() : nullptr;
            if (root != nullptr) { AddGroupNode(root, 0); }
            // Re-validate the selection against the fresh snapshot (the group may be gone).
            bool selectionAlive = false;
            for (const GroupNode& node : m_groups) { if (node.group == m_selectedGroup) { selectionAlive = true; break; } }
            if (!selectionAlive) { m_selectedGroup = root; }

            m_tree->SetAdapter(m_treeAdapter.Get());
            ui::FlattenedTreeAdapter* flat = m_tree->FlatAdapter();
            for (usize i = 0; i < m_groups.Size(); ++i)
            {
                if (!m_groups[i].children.IsEmpty()) { flat->Expand(static_cast<i32>(i)); }
            }
            RebuildList();
        }

        // Fill the available space.
        void OnMeasure(ui::BoxConstraints constraints) override
        {
            for (usize i = 0; i < ChildCount(); ++i) { GetChildAt(i)->Measure(constraints); }
            MeasuredSize = Float2{ constraints.MaxWidth, constraints.MaxHeight };
        }
        void OnLayout(f32, f32, f32 width, f32 height) override
        {
            for (usize i = 0; i < ChildCount(); ++i) { GetChildAt(i)->Layout(0, 0, width, height); }
        }

    private:
        struct GroupNode
        {
            content::Group* group = nullptr;
            i32 depth = 0;
            Array<i32> children;
        };

        // One content-area row: a SUBGROUP of the selected group (group != null) or an instance.
        struct Row
        {
            Guid id;                              // instance identity (safe across deletes)
            content::Group* group = nullptr;      // non-null => a navigable subgroup row
        };

        // The name element of every row/tile/tree-node: an EditableLabel that knows WHAT it
        // names, so one commit handler routes to the instance or group rename. Slow-click /
        // F2 / menu Rename edit in place; double-click is deliberately NOT an edit trigger
        // (it navigates); single clicks pass through to the list's selection.
        class NameLabel final : public ui::EditableLabel
        {
        public:
            void BindTarget(const Guid& id, content::Group* group)
            {
                m_id = id;
                m_group = group;
            }
            [[nodiscard]] const Guid& TargetId() const noexcept { return m_id; }
            [[nodiscard]] content::Group* TargetGroup() const noexcept { return m_group; }
        private:
            Guid m_id;
            content::Group* m_group = nullptr;
        };

        // Shared setup: names are filenames/directory names, so filesystem-hostile
        // characters never commit; double-click stays navigation.
        static void ConfigureNameLabel(NameLabel& label, AssetsView& owner)
        {
            label.DoubleClickToEdit.SetValue(false);
            label.ValidateRename = [](StringView name) {
                for (usize i = 0; i < name.Size(); ++i)
                {
                    const utf8char c = name[i];
                    if (c == utf8char('/') || c == utf8char('\\') || c == utf8char(':')
                        || c == utf8char('*') || c == utf8char('?') || c == utf8char('"')
                        || c == utf8char('<') || c == utf8char('>') || c == utf8char('|'))
                    {
                        return false;
                    }
                }
                return true;
            };
            AssetsView* self = &owner;
            NameLabel* raw = &label;
            label.OnRenameCommitted.Add([self, raw](ui::EditableLabel*, StringView newName) {
                self->ApplyRename(raw, newName);
            });
        }

        // === adapters ===

        class TreeAdapter final : public ui::ITreeAdapter
        {
        public:
            explicit TreeAdapter(AssetsView& owner) : m_owner(&owner) {}
            [[nodiscard]] i32 RootCount() const override { return m_owner->m_groups.IsEmpty() ? 0 : 1; }
            [[nodiscard]] i32 GetChildCount(i32 nodeId) const override
            {
                if (nodeId == -1) { return RootCount(); }
                return InRange(nodeId) ? static_cast<i32>(Node(nodeId).children.Size()) : 0;
            }
            [[nodiscard]] i32 GetChildId(i32 parentId, i32 childIndex) const override
            {
                if (parentId == -1) { return childIndex == 0 && !m_owner->m_groups.IsEmpty() ? 0 : -1; }
                if (!InRange(parentId)) { return -1; }
                const Array<i32>& kids = Node(parentId).children;
                return (childIndex >= 0 && childIndex < static_cast<i32>(kids.Size()))
                    ? kids[static_cast<usize>(childIndex)] : -1;
            }
            [[nodiscard]] i32 GetDepth(i32 nodeId) const override
            {
                return InRange(nodeId) ? Node(nodeId).depth : 0;
            }
            [[nodiscard]] bool HasChildren(i32 nodeId) const override { return GetChildCount(nodeId) > 0; }
            [[nodiscard]] RefPtr<ui::View> CreateView(i32) override
            {
                auto row = MakeRef<NameLabel>(DefaultAllocator());
                row->FontSize.SetValue(13.0f);
                ConfigureNameLabel(*row, *m_owner);
                return RefPtr<ui::View>(row.Get());
            }
            void BindView(ui::View* view, i32 nodeId, i32 depth, bool) override
            {
                if (!InRange(nodeId)) { return; }
                auto* row = static_cast<NameLabel*>(view);
                const GroupNode& node = Node(nodeId);
                const bool isRoot = node.group->Parent() == nullptr;
                row->BindTarget(Guid{}, node.group);
                row->SetText(isRoot ? StringView(u8"Content") : node.group->Name());
                row->SlowClickToEdit.SetValue(!isRoot);   // the root is not renamable
                row->TextOffsetX.SetValue(static_cast<f32>(depth + 1) * 18.0f);
            }
        private:
            [[nodiscard]] bool InRange(i32 nodeId) const
            {
                return nodeId >= 0 && nodeId < static_cast<i32>(m_owner->m_groups.Size());
            }
            [[nodiscard]] const GroupNode& Node(i32 nodeId) const
            {
                return m_owner->m_groups[static_cast<usize>(nodeId)];
            }
            AssetsView* m_owner;
        };

        class ListAdapter final : public ui::ListAdapterBase
        {
        public:
            explicit ListAdapter(AssetsView& owner) : m_owner(&owner) {}
            [[nodiscard]] i32 ItemCount() const override
            {
                return static_cast<i32>(m_owner->m_rows.Size());
            }
            [[nodiscard]] RefPtr<ui::View> CreateView(i32) override
            {
                auto row = MakeRef<ui::FlexLayout>(DefaultAllocator());
                row->Direction = ui::Orientation::Horizontal;
                row->Spacing = 6;
                row->Padding = ui::Thickness{ 4, 3 };
                auto iconView = MakeRef<ui::DrawableView>(DefaultAllocator());
                iconView->DesiredWidth.SetValue(Optional<f32>(16.0f));
                iconView->DesiredHeight.SetValue(Optional<f32>(16.0f));
                row->AddView(iconView.Get());
                auto name = MakeRef<NameLabel>(DefaultAllocator());
                name->FontSize.SetValue(13.0f);
                ConfigureNameLabel(*name, *m_owner);
                auto grow = MakeRef<ui::FlexLayoutParams>(DefaultAllocator());
                grow->Grow = 1.0f;
                row->AddView(name.Get(), grow);
                auto meta = MakeRef<ui::Label>(DefaultAllocator());
                meta->FontSize.SetValue(13.0f);
                row->AddView(meta.Get());
                return RefPtr<ui::View>(row.Get());
            }
            void BindView(ui::View* view, i32 position) override
            {
                auto* row = Cast<ui::FlexLayout>(view);
                if (row == nullptr || row->ChildCount() < 3) { return; }
                auto* iconView = Cast<ui::DrawableView>(row->GetChildAt(0));
                auto* name = static_cast<NameLabel*>(row->GetChildAt(1));
                auto* meta = Cast<ui::Label>(row->GetChildAt(2));
                if (iconView == nullptr || name == nullptr || meta == nullptr) { return; }
                iconView->Drawable = ui::DrawablePtr(m_owner->RowIcon(position));
                const Row* target = m_owner->RowAt(position);
                name->BindTarget(target != nullptr ? target->id : Guid{},
                                 target != nullptr ? target->group : nullptr);
                String text;
                Color color{ 0.85f, 0.85f, 0.85f, 1.0f };
                m_owner->RowName(position, text, color);
                name->SetText(text.AsView());
                name->TextColor.SetValue(Optional<Color>(color));
                String metaText;
                Color metaColor{ 0.55f, 0.58f, 0.65f, 1.0f };
                m_owner->RowMeta(position, metaText, metaColor);
                meta->SetText(metaText.AsView());
                meta->TextColor.SetValue(Optional<Color>(metaColor));
            }
        private:
            AssetsView* m_owner;
        };

        // Grid tiles: the same rows as the list, name under a (future-thumbnail) type block.
        class GridAdapter final : public ui::ListAdapterBase
        {
        public:
            explicit GridAdapter(AssetsView& owner) : m_owner(&owner) {}
            [[nodiscard]] i32 ItemCount() const override
            {
                return static_cast<i32>(m_owner->m_rows.Size());
            }
            [[nodiscard]] RefPtr<ui::View> CreateView(i32) override
            {
                auto tile = MakeRef<ui::FlexLayout>(DefaultAllocator());
                tile->Direction = ui::Orientation::Vertical;
                tile->Padding = ui::Thickness{ 4, 4 };
                auto iconRow = MakeRef<ui::FlexLayout>(DefaultAllocator());
                iconRow->Direction = ui::Orientation::Horizontal;
                iconRow->JustifyContent = ui::Justify::Center;
                // Cross-axis default is Stretch - without Center the icon fills the row's
                // height (taller than 40) and the glyph draws vertically stretched.
                iconRow->AlignItems = ui::Align::Center;
                auto iconView = MakeRef<ui::DrawableView>(DefaultAllocator());
                iconView->DesiredWidth.SetValue(Optional<f32>(40.0f));
                iconView->DesiredHeight.SetValue(Optional<f32>(40.0f));
                iconRow->AddView(iconView.Get());
                auto name = MakeRef<NameLabel>(DefaultAllocator());
                name->FontSize.SetValue(12.0f);
                name->HAlign.SetValue(draconic::fonts::TextAlignment::Center);
                name->Ellipsis.SetValue(true);   // long asset names truncate with "..." instead of overflowing the tile
                ConfigureNameLabel(*name, *m_owner);
                {
                    auto grow = MakeRef<ui::FlexLayoutParams>(DefaultAllocator());
                    grow->Grow = 1.0f;
                    grow->Width = ui::SizeSpec::Match();
                    tile->AddView(iconRow.Get(), grow);
                }
                {
                    auto lp = MakeRef<ui::FlexLayoutParams>(DefaultAllocator());
                    lp->Width = ui::SizeSpec::Match();
                    lp->Height = ui::SizeSpec::Fixed(ui::Unit::Px(18));
                    tile->AddView(name.Get(), lp);
                }
                return RefPtr<ui::View>(tile.Get());
            }
            void BindView(ui::View* view, i32 position) override
            {
                auto* tile = Cast<ui::FlexLayout>(view);
                if (tile == nullptr || tile->ChildCount() < 2) { return; }
                auto* iconRow = Cast<ui::FlexLayout>(tile->GetChildAt(0));
                auto* name = static_cast<NameLabel*>(tile->GetChildAt(1));
                if (iconRow == nullptr || iconRow->ChildCount() < 1 || name == nullptr) { return; }
                auto* iconView = Cast<ui::DrawableView>(iconRow->GetChildAt(0));
                const Row* row = m_owner->RowAt(position);
                if (iconView == nullptr || row == nullptr) { return; }
                iconView->Drawable = ui::DrawablePtr(m_owner->RowIcon(position));
                name->BindTarget(row->id, row->group);
                // The tile has no meta label: the name carries the badge color (favorite
                // gold wins) and stays PURE (it is the inline-rename edit text).
                String text;
                Color color{ 0.85f, 0.85f, 0.85f, 1.0f };
                m_owner->RowName(position, text, color);
                name->SetText(text.AsView());
                name->TextColor.SetValue(Optional<Color>(color));
            }
        private:
            AssetsView* m_owner;
        };

        // === model ===

        i32 AddGroupNode(content::Group* group, i32 depth)
        {
            const i32 nodeId = static_cast<i32>(m_groups.Size());
            GroupNode node;
            node.group = group;
            node.depth = depth;
            m_groups.PushBack(Move(node));
            for (content::Group* child : group->Groups())
            {
                const i32 childId = AddGroupNode(child, depth + 1);
                m_groups[static_cast<usize>(nodeId)].children.PushBack(childId);
            }
            return nodeId;
        }

        void SelectGroup(content::Group* group)
        {
            m_selectedGroup = group;
            // Group navigation replaces the search scope: a stale filter here reads as "my
            // group is empty" (the filter searches ALL groups, ignoring the selection).
            if (!m_filter.IsEmpty())
            {
                m_filter.Clear();
                m_filterEdit->SetText(u8"");
            }
            RebuildList();
        }

        void RebuildList()
        {
            m_rows.Clear();
            if (m_context->Project() != nullptr)
            {
                if (m_filter.IsEmpty())
                {
                    if (m_selectedGroup != nullptr)
                    {
                        for (content::Group* child : m_selectedGroup->Groups())
                        {
                            Row row;
                            row.group = child;
                            m_rows.PushBack(row);
                        }
                        for (content::Instance* instance : m_selectedGroup->Instances())
                        {
                            Row row;
                            row.id = instance->Id();
                            m_rows.PushBack(row);
                        }
                    }
                }
                else
                {
                    CollectFiltered(m_context->Project()->SourceDb().RootGroup());
                }
            }
            m_list->Selection.ClearSelection();
            m_grid->Selection.ClearSelection();
            m_list->NotifyDataChanged();
            m_gridAdapter->NotifyDataSetChanged();
            UpdateBreadcrumb();
        }

        void CollectFiltered(content::Group* group)
        {
            if (group == nullptr) { return; }
            for (content::Instance* instance : group->Instances())
            {
                if (MatchesFilter(instance->Name(), m_filter.AsView()))
                {
                    Row row;
                    row.id = instance->Id();
                    m_rows.PushBack(row);
                }
            }
            for (content::Group* child : group->Groups()) { CollectFiltered(child); }
        }

        [[nodiscard]] static bool MatchesFilter(StringView name, StringView filter)
        {
            if (filter.IsEmpty()) { return true; }
            if (name.Size() < filter.Size()) { return false; }
            auto lower = [](utf8char c) {
                return (c >= utf8char('A') && c <= utf8char('Z')) ? static_cast<utf8char>(c + 32) : c;
            };
            for (usize i = 0; i + filter.Size() <= name.Size(); ++i)
            {
                bool match = true;
                for (usize j = 0; j < filter.Size(); ++j)
                {
                    if (lower(name[i + j]) != lower(filter[j])) { match = false; break; }
                }
                if (match) { return true; }
            }
            return false;
        }

        [[nodiscard]] const Row* RowAt(i32 position) const
        {
            if (position < 0 || position >= static_cast<i32>(m_rows.Size())) { return nullptr; }
            return &m_rows[static_cast<usize>(position)];
        }

        [[nodiscard]] content::Instance* InstanceAt(i32 position)
        {
            const Row* row = RowAt(position);
            return (row != nullptr && row->group == nullptr) ? Resolve(row->id) : nullptr;
        }

        [[nodiscard]] ui::Drawable* RowIcon(i32 position)
        {
            const Row* row = RowAt(position);
            if (row == nullptr) { return nullptr; }
            EditorIcons& icons = EditorIcons::Get();
            if (row->group != nullptr) { return icons.folder.Get(); }
            content::Instance* instance = Resolve(row->id);
            return (instance != nullptr) ? icons.ForAssetType(instance->TypeName()) : nullptr;
        }

        // The name text stays PURE (it doubles as the inline-rename edit text): favorites
        // show as GOLD, not a "* " prefix; the badge tints it when not favorited.
        void RowName(i32 position, String& text, Color& color)
        {
            const Row* row = RowAt(position);
            if (row == nullptr) { return; }
            if (row->group != nullptr)
            {
                text.Append(row->group->Name());
                color = Color{ 0.85f, 0.75f, 0.5f, 1.0f };
                return;
            }
            content::Instance* instance = Resolve(row->id);
            if (instance == nullptr) { return; }
            text.Append(instance->Name());
            if (m_context->IsFavorite(row->id)) { color = Color{ 0.95f, 0.8f, 0.35f, 1.0f }; return; }
            switch (m_cook->BadgeFor(*instance))
            {
                case draconic::editor::CookBadge::Cooked:  color = Color{ 0.6f, 0.9f, 0.6f, 1.0f }; break;
                case draconic::editor::CookBadge::Missing: color = Color{ 0.95f, 0.85f, 0.5f, 1.0f }; break;
                case draconic::editor::CookBadge::Failed:  color = Color{ 1.0f, 0.45f, 0.45f, 1.0f }; break;
                case draconic::editor::CookBadge::NoBuilder: break;
            }
        }

        // The trailing meta label (list mode only): type + cook badge, badge-colored.
        void RowMeta(i32 position, String& text, Color& color)
        {
            const Row* row = RowAt(position);
            if (row == nullptr) { return; }
            if (row->group != nullptr)
            {
                text.Append(u8"Group");
                return;
            }
            content::Instance* instance = Resolve(row->id);
            if (instance == nullptr) { return; }
            text.Append(instance->TypeName());
            switch (m_cook->BadgeFor(*instance))
            {
                case draconic::editor::CookBadge::Cooked:
                    text.Append(u8"  [cooked]");
                    color = Color{ 0.6f, 0.9f, 0.6f, 1.0f };
                    break;
                case draconic::editor::CookBadge::Missing:
                    text.Append(u8"  [not cooked]");
                    color = Color{ 0.95f, 0.85f, 0.5f, 1.0f };
                    break;
                case draconic::editor::CookBadge::Failed:
                    text.Append(u8"  [FAILED]");
                    color = Color{ 1.0f, 0.45f, 0.45f, 1.0f };
                    break;
                case draconic::editor::CookBadge::NoBuilder:
                    break;
            }
        }

        // === navigation ===

        void ActivateRow(i32 position, i32 clickCount)
        {
            if (clickCount < 2) { return; }
            const Row* row = RowAt(position);
            if (row == nullptr) { return; }
            if (row->group != nullptr) { SelectGroup(row->group); return; }
            if (content::Instance* instance = Resolve(row->id))
            {
                if (OnOpenInstance) { OnOpenInstance(*instance); }
            }
        }

        void UpdateBreadcrumb()
        {
            // Root -> selected group as clickable segments ("Content / models / fox").
            Array<content::Group*> chain;
            for (content::Group* g = m_selectedGroup; g != nullptr; g = g->Parent()) { chain.PushBack(g); }
            m_breadcrumbGroups.Clear();
            Array<StringView> segments;
            for (usize i = chain.Size(); i > 0; --i)
            {
                content::Group* g = chain[i - 1];
                m_breadcrumbGroups.PushBack(g);
                segments.PushBack(g->Parent() == nullptr ? StringView(u8"Content") : g->Name());
            }
            m_breadcrumb->SetSegments(Span<StringView>{ segments.Data(), segments.Size() });
        }

        void NavigateToBreadcrumb(i32 segment)
        {
            if (segment >= 0 && segment < static_cast<i32>(m_breadcrumbGroups.Size()))
            {
                SelectGroup(m_breadcrumbGroups[static_cast<usize>(segment)]);
            }
        }

        void SetGridMode(bool grid)
        {
            m_gridMode = grid;
            m_listToggle->IsChecked.SetValue(!grid);
            m_gridToggle->IsChecked.SetValue(grid);
            m_list->Visibility = grid ? ui::VisibilityValue::Gone : ui::VisibilityValue::Visible;
            m_grid->Visibility = grid ? ui::VisibilityValue::Visible : ui::VisibilityValue::Gone;
            Invalidate();
        }

        // === menus ===

        // Selected INSTANCE ids in the active view (group rows never join the selection set for
        // destructive actions; `clicked` is always included).
        [[nodiscard]] Array<Guid> SelectedInstanceIds(ui::SelectionModel* selection, i32 clicked)
        {
            Array<Guid> ids;
            auto push = [&](i32 position) {
                const Row* row = RowAt(position);
                if (row == nullptr || row->group != nullptr) { return; }
                for (const Guid& existing : ids) { if (existing == row->id) { return; } }
                ids.PushBack(row->id);
            };
            if (selection != nullptr && selection->IsSelected(clicked))
            {
                for (i32 position : selection->SelectedPositions()) { push(position); }
            }
            else { push(clicked); }
            return ids;
        }

        void ShowRowMenu(ui::View* anchor, ui::SelectionModel* selection, i32 position, f32 x, f32 y)
        {
            if (Context == nullptr) { return; }
            const Row* row = RowAt(position);
            if (row == nullptr) { return; }
            AssetsView* self = this;

            // Group rows: navigate / rename in place / delete (recursive, confirmed).
            if (row->group != nullptr)
            {
                content::Group* group = row->group;
                auto menu = MakeRef<ui::ContextMenu>(DefaultAllocator());
                menu->AddItem(u8"Open", [self, group]() { self->SelectGroup(group); });
                menu->AddItem(u8"Cook Group", [self, group]() {
                    Array<Guid> ids;
                    CollectInstanceIds(group, ids);
                    self->m_cook->RequestCookFor(Move(ids), false);
                });
                menu->AddItem(u8"Rebuild Group", [self, group]() {
                    Array<Guid> ids;
                    CollectInstanceIds(group, ids);
                    self->m_cook->RequestCookFor(Move(ids), true);
                });
                menu->AddItem(u8"Rename", [self, position]() { self->StartRenameDeferred(position); });
                menu->AddSeparator();
                menu->AddItem(u8"Delete Group", [self, group]() { self->ConfirmDeleteGroup(group); });
                const Float2 screenPos = anchor->LocalToScreen(Float2{ x, y });
                menu->Show(Context, screenPos.x, screenPos.y);
                return;
            }

            content::Instance* instance = Resolve(row->id);
            if (instance == nullptr) { return; }
            const Guid id = row->id;
            Array<Guid> targets = SelectedInstanceIds(selection, position);

            auto menu = MakeRef<ui::ContextMenu>(DefaultAllocator());
            menu->AddItem(u8"Open", [self, id]() {
                if (content::Instance* inst = self->Resolve(id))
                {
                    if (self->OnOpenInstance) { self->OnOpenInstance(*inst); }
                }
            });
            menu->AddItem(u8"Rename", [self, position]() { self->StartRenameDeferred(position); });
            menu->AddItem(u8"Duplicate", [self, id]() { self->DuplicateInstance(id); });
            menu->AddItem(m_context->IsFavorite(id) ? StringView(u8"Unpin favorite")
                                                    : StringView(u8"Pin favorite"),
                          [self, id]() {
                              self->m_context->ToggleFavorite(id);
                              self->RebuildList();
                          });
            menu->AddSeparator();
            // Scoped: the selected assets + their dependency closure (Build > Cook All stays
            // the whole-project path) - huge scenes cook one asset/group at a time.
            menu->AddItem(u8"Cook", [self, targets]() {
                Array<Guid> ids = targets;
                self->m_cook->RequestCookFor(Move(ids), false);
            });
            menu->AddItem(u8"Rebuild", [self, targets]() {
                Array<Guid> ids = targets;
                self->m_cook->RequestCookFor(Move(ids), true);
            });
            menu->AddSeparator();
            String deleteLabel(u8"Delete");
            if (targets.Size() > 1)
            {
                deleteLabel += u8" ";
                AppendCount(deleteLabel, targets.Size());
                deleteLabel += u8" assets";
            }
            menu->AddItem(deleteLabel.AsView(), [self, targets]() { self->ConfirmDelete(targets); });
            const Float2 screenPos = anchor->LocalToScreen(Float2{ x, y });
            menu->Show(Context, screenPos.x, screenPos.y);
        }

        void ShowBackgroundMenu(ui::View* anchor, f32 x, f32 y)
        {
            if (Context == nullptr) { return; }
            AssetsView* self = this;
            content::Group* target = m_selectedGroup;   // creations land in the group we're in
            auto menu = MakeRef<ui::ContextMenu>(DefaultAllocator());

            // Top-level creators, then categorized ones ("Primitives") in submenus, then the
            // group + cook actions.
            Array<StringView> categories;
            for (const draconic::editor::EditorContext::AssetCreator& creator : m_context->Creators())
            {
                if (creator.category.IsEmpty())
                {
                    String label(u8"New ");
                    label += creator.label;
                    const auto* entry = &creator;
                    menu->AddItem(label.AsView(), [self, entry, target]() {
                        if (self->OnCreate) { self->OnCreate(*entry, target); }
                        self->Rebuild();
                    });
                    continue;
                }
                bool seen = false;
                for (StringView c : categories) { if (c == creator.category.AsView()) { seen = true; break; } }
                if (!seen) { categories.PushBack(creator.category.AsView()); }
            }
            menu->AddItem(u8"New Group", [self, target]() { self->CreateGroupIn(target); });
            if (target != nullptr)
            {
                menu->AddItem(u8"Cook Group", [self, target]() {
                    Array<Guid> ids;
                    CollectInstanceIds(target, ids);
                    self->m_cook->RequestCookFor(Move(ids), false);
                });
                menu->AddItem(u8"Rebuild Group", [self, target]() {
                    Array<Guid> ids;
                    CollectInstanceIds(target, ids);
                    self->m_cook->RequestCookFor(Move(ids), true);
                });
            }
            if (target != nullptr && target->Parent() != nullptr)
            {
                menu->AddItem(u8"Rename Group", [self, target]() {
                    self->StartRenameGroupInTreeDeferred(target);
                });
                menu->AddItem(u8"Delete Group", [self, target]() {
                    self->ConfirmDeleteGroup(target);
                });
            }
            if (!categories.IsEmpty()) { menu->AddSeparator(); }
            for (StringView category : categories)
            {
                ui::MenuItem* submenuItem = menu->AddSubmenu(category);
                auto* submenu = Cast<ui::ContextMenu>(submenuItem->Submenu.Get());
                if (submenu == nullptr) { continue; }
                for (const draconic::editor::EditorContext::AssetCreator& creator : m_context->Creators())
                {
                    if (creator.category.AsView() != category) { continue; }
                    const auto* entry = &creator;
                    submenu->AddItem(creator.label.AsView(), [self, entry, target]() {
                        if (self->OnCreate) { self->OnCreate(*entry, target); }
                        self->Rebuild();
                    });
                }
            }
            menu->AddSeparator();
            menu->AddItem(u8"Cook All", [self]() { self->m_cook->RequestCook(false); });
            menu->AddItem(u8"Rebuild All", [self]() { self->m_cook->RequestCook(true); });
            const Float2 screenPos = anchor->LocalToScreen(Float2{ x, y });
            menu->Show(Context, screenPos.x, screenPos.y);
        }

        void CreateGroupIn(content::Group* parent)
        {
            // Cook gate: see ImportFile (structural DB mutation while the plan worker reads).
            if (m_cook->MutationLocked())
            {
                AssetsView* self = this;
                m_cook->RunWhenIdle(Function<void()>{ [self, parent]() { self->CreateGroupIn(parent); } });
                m_context->Notify(draconic::editor::NoticeKind::Info,
                                  u8"New group queued until the current cook finishes.");
                return;
            }
            if (parent == nullptr || m_context->Project() == nullptr) { return; }
            String name(u8"Group");
            for (i32 counter = 2; parent->GetGroup(name.AsView()) != nullptr; ++counter)
            {
                name = String(u8"Group");
                if (counter >= 10) { name.PushBack(static_cast<utf8char>('0' + (counter / 10 % 10))); }
                name.PushBack(static_cast<utf8char>('0' + (counter % 10)));
            }
            content::Group* created = parent->CreateGroup(name.AsView());
            if (created != nullptr)
            {
                DRACONIC_LOG_INFO(u8"Assets", u8"created group '{}'", created->Path());
                m_selectedGroup = created;
                Rebuild();
            }
        }

        // === actions ===

        void DuplicateInstance(const Guid& id)
        {
            content::Instance* src = Resolve(id);
            if (src == nullptr || m_context->Project() == nullptr) { return; }
            content::ContentDatabase& db = m_context->Project()->SourceDb();

            // "name2", "name3", ... in the source's own group.
            String name;
            for (i32 counter = 2; ; ++counter)
            {
                name = String(src->Name());
                if (counter >= 10) { name.PushBack(static_cast<utf8char>('0' + (counter / 10 % 10))); }
                name.PushBack(static_cast<utf8char>('0' + (counter % 10)));
                if (src->OwningGroup().GetInstance(name.AsView()) == nullptr) { break; }
            }
            content::Instance* copy = db.CloneInstance(id, name.AsView());
            if (copy == nullptr)
            {
                m_context->Notify(draconic::editor::NoticeKind::Error,
                                  u8"Duplicate FAILED (see console).");
                return;
            }
            DRACONIC_LOG_INFO(u8"Assets", u8"duplicated '{}' -> '{}'", src->Path(), copy->Path());
            String message(u8"Duplicated as '");
            message += copy->Name();
            message += u8"'.";
            m_context->SetStatus(message.AsView());
            Rebuild();
            m_cook->RequestCook(false);   // builder-backed clones become pickable right away
        }

        // === inline rename ===

        // NameLabel commit handler: route to the instance or group apply.
        void ApplyRename(NameLabel* label, StringView newName)
        {
            if (label->TargetGroup() != nullptr) { ApplyRenameGroup(label->TargetGroup(), newName); }
            else { ApplyRenameInstance(label->TargetId(), newName); }
        }

        void ApplyRenameInstance(const Guid& id, StringView name)
        {
            // Cook gate (renames move files + rewrite both DBs' entries).
            if (m_cook->MutationLocked())
            {
                AssetsView* self = this;
                m_cook->RunWhenIdle(Function<void()>{ [self, id, renamed = String(name)]() {
                    self->ApplyRenameInstance(id, renamed.AsView());
                } });
                m_context->Notify(draconic::editor::NoticeKind::Info,
                                  u8"Rename queued until the current cook finishes.");
                return;
            }
            if (m_context->Project() == nullptr) { return; }
            content::Instance* inst = Resolve(id);
            if (inst == nullptr) { return; }
            const String oldPath = inst->Path();
            const Status renamed = m_context->Project()->SourceDb().RenameInstance(id, name);
            if (!renamed.IsOk())
            {
                m_context->Notify(draconic::editor::NoticeKind::Error,
                    renamed.Code() == ErrorCode::AlreadyExists
                        ? StringView(u8"NOT renamed: name already taken.")
                        : StringView(u8"Rename FAILED (see console)."));
                Rebuild();   // snap the label back to the real name
                return;
            }
            // Keep the cooked product's name in step (same guid; purely cosmetic -
            // everything binds by guid - but stale names in Cooked/ confuse).
            (void)m_context->Project()->CookedDb().RenameInstance(id, name);
            // The manifest's default scene is guid-authoritative; refresh the
            // human-readable path mirror. The path compare covers guid-less
            // manifests (and adopts the guid while at it).
            auto* project = m_context->Project();
            if (project->Settings().defaultSceneId == id
                || project->Settings().defaultScene == oldPath)
            {
                project->Settings().defaultSceneId = id;
                project->Settings().defaultScene = inst->Path();
                (void)project->SaveSettings();
            }
            DRACONIC_LOG_INFO(u8"Assets", u8"renamed '{}' -> '{}'", oldPath, inst->Path());
            Rebuild();
        }

        void ApplyRenameGroup(content::Group* group, StringView name)
        {
            // Cook gate: see ApplyRenameInstance.
            if (m_cook->MutationLocked())
            {
                AssetsView* self = this;
                m_cook->RunWhenIdle(Function<void()>{ [self, group, renamed = String(name)]() {
                    self->ApplyRenameGroup(group, renamed.AsView());
                } });
                m_context->Notify(draconic::editor::NoticeKind::Info,
                                  u8"Rename queued until the current cook finishes.");
                return;
            }
            if (m_context->Project() == nullptr) { return; }
            const String oldPath = group->Path();
            const Status renamed = m_context->Project()->SourceDb().RenameGroup(*group, name);
            if (!renamed.IsOk())
            {
                m_context->Notify(draconic::editor::NoticeKind::Error,
                    renamed.Code() == ErrorCode::AlreadyExists
                        ? StringView(u8"NOT renamed: name already taken.")
                        : StringView(u8"Rename FAILED (see console)."));
                Rebuild();
                return;
            }
            // Mirror in the cooked DB when a same-path group exists there.
            content::Group* cooked = m_context->Project()->CookedDb().RootGroup();
            usize start = 0;
            const StringView path = oldPath.AsView();
            for (usize i = 0; i <= path.Size() && cooked != nullptr; ++i)
            {
                if (i == path.Size() || path[i] == utf8char('/'))
                {
                    if (i > start) { cooked = cooked->GetGroup(path.SubStr(start, i - start)); }
                    start = i + 1;
                }
            }
            if (cooked != nullptr)
            {
                (void)m_context->Project()->CookedDb().RenameGroup(*cooked, name);
            }
            // Refresh the default scene's path mirror if it lived under the renamed
            // group (guid still resolves; the mirror is cosmetic but shouldn't lie).
            auto* project = m_context->Project();
            if (!project->Settings().defaultSceneId.IsNil())
            {
                if (content::Instance* ds = project->SourceDb().GetInstance(
                        project->Settings().defaultSceneId))
                {
                    if (project->Settings().defaultScene != ds->Path())
                    {
                        project->Settings().defaultScene = ds->Path();
                        (void)project->SaveSettings();
                    }
                }
            }
            else
            {
                const StringView ds = project->Settings().defaultScene.AsView();
                if (ds.Size() > oldPath.Size()
                    && ds.SubStr(0, oldPath.Size()) == oldPath.AsView()
                    && ds[oldPath.Size()] == utf8char('/'))
                {
                    String updated(group->Path());
                    updated.Append(ds.SubStr(oldPath.Size(), ds.Size() - oldPath.Size()));
                    project->Settings().defaultScene = Move(updated);
                    (void)project->SaveSettings();
                }
            }
            DRACONIC_LOG_INFO(u8"Assets", u8"renamed group '{}' -> '{}'", oldPath, group->Path());
            Rebuild();
        }

        // Begin the in-place edit of a content-area row (menu Rename path is DOUBLE-deferred
        // through the mutation queue - Sedulous lesson: BeginEdit's SetFocus must land AFTER
        // the menu's ClosePopup/PopFocus restored focus, one queue drain is not enough).
        void StartRename(i32 position)
        {
            NameLabel* label = nullptr;
            if (m_gridMode)
            {
                m_grid->ScrollToPosition(position);
                if (auto* tile = Cast<ui::FlexLayout>(m_grid->GetActiveView(position)))
                {
                    if (tile->ChildCount() >= 2) { label = static_cast<NameLabel*>(tile->GetChildAt(1)); }
                }
            }
            else
            {
                m_list->ScrollToPosition(position);
                if (auto* row = Cast<ui::FlexLayout>(m_list->GetActiveView(position)))
                {
                    if (row->ChildCount() >= 2) { label = static_cast<NameLabel*>(row->GetChildAt(1)); }
                }
            }
            if (label != nullptr) { label->BeginEdit(); }
        }

        void StartRenameDeferred(i32 position)
        {
            ui::UIContext* ctx = Context;
            if (ctx == nullptr) { return; }
            AssetsView* self = this;
            ctx->MutationQueueRef().QueueAction(Function<void()>{ [self, ctx, position]() {
                ctx->MutationQueueRef().QueueAction(Function<void()>{ [self, position]() {
                    self->StartRename(position);
                } });
            } });
        }

        // In-place edit of a group's TREE row (the background/tree menu path).
        void StartRenameGroupInTree(content::Group* group)
        {
            ui::FlattenedTreeAdapter* flat = m_tree->FlatAdapter();
            if (flat == nullptr) { return; }
            for (i32 pos = 0; pos < flat->ItemCount(); ++pos)
            {
                const i32 nodeId = flat->GetNodeId(pos);
                if (nodeId >= 0 && nodeId < static_cast<i32>(m_groups.Size())
                    && m_groups[static_cast<usize>(nodeId)].group == group)
                {
                    m_tree->InternalListView()->ScrollToPosition(pos);
                    if (auto* row = static_cast<NameLabel*>(m_tree->InternalListView()->GetActiveView(pos)))
                    {
                        row->BeginEdit();
                    }
                    return;
                }
            }
        }

        void StartRenameGroupInTreeDeferred(content::Group* group)
        {
            ui::UIContext* ctx = Context;
            if (ctx == nullptr) { return; }
            AssetsView* self = this;
            ctx->MutationQueueRef().QueueAction(Function<void()>{ [self, ctx, group]() {
                ctx->MutationQueueRef().QueueAction(Function<void()>{ [self, group]() {
                    self->StartRenameGroupInTree(group);
                } });
            } });
        }

        void ConfirmDelete(Array<Guid> ids)
        {
            if (ids.IsEmpty() || Context == nullptr) { return; }
            String message;
            if (ids.Size() == 1)
            {
                content::Instance* instance = Resolve(ids[0]);
                if (instance == nullptr) { return; }
                message += u8"Delete '";
                message += instance->Name();
                message += u8"'? Its source file and cooked product go away; open pages close.";
            }
            else
            {
                message += u8"Delete ";
                AppendCount(message, ids.Size());
                message += u8" assets? Their source files and cooked products go away; open pages close.";
            }

            AssetsView* self = this;
            RefPtr<ui::Dialog> dialog = ui::Dialog::Confirm(u8"Delete assets", message.AsView());
            dialog->OnClosed.Add(ui::Event<void(ui::Dialog*, ui::DialogResult)>::Handler{
                [self, ids](ui::Dialog*, ui::DialogResult result) {
                    if (result != ui::DialogResult::OK) { return; }
                    // Deferred: page teardown + DB mutation never run mid-event-dispatch.
                    ui::UIContext* ctx = self->Context;
                    if (ctx == nullptr) { return; }
                    Array<Guid> targets = ids;
                    ctx->MutationQueueRef().QueueAction(Function<void()>{ [self, targets]() {
                        self->DeleteInstances(targets);
                    } });
                } });
            dialog->Show(Context);
        }

        // Runs from the mutation queue: close pages, delete, log, refresh.
        void DeleteInstances(const Array<Guid>& ids)
        {
            // Cook gate: see ImportFile/DeleteGroupNow.
            if (m_cook->MutationLocked())
            {
                AssetsView* self = this;
                Array<Guid> copy = ids;
                m_cook->RunWhenIdle(Function<void()>{ [self, copy = Move(copy)]() {
                    self->DeleteInstances(copy);
                } });
                m_context->Notify(draconic::editor::NoticeKind::Info,
                                  u8"Delete queued until the current cook finishes.");
                return;
            }
            if (m_context->Project() == nullptr) { return; }
            content::ContentDatabase& db = m_context->Project()->SourceDb();
            usize deleted = 0;
            for (const Guid& id : ids)
            {
                content::Instance* instance = db.GetInstance(id);
                if (instance == nullptr) { continue; }
                const String path = instance->Path();
                if (OnCloseInstancePage) { OnCloseInstancePage(id); }
                if (db.DeleteInstance(id).IsOk())
                {
                    ++deleted;
                    DRACONIC_LOG_INFO(u8"Assets", u8"deleted '{}'", path);
                }
                else
                {
                    DRACONIC_LOG_WARNING(u8"Assets", u8"delete FAILED for '{}'", path);
                }
            }
            String message(u8"Deleted ");
            AppendCount(message, deleted);
            message += u8" asset(s).";
            m_context->SetStatus(message.AsView());
            ClearDefaultSceneIfGone();
            Rebuild();   // the next cook's plan sweeps the orphaned products
        }

        // F2 = inline rename, Delete = confirmed delete - dispatched by ListView/GridView
        // before their own navigation keys.
        void OnRowKeyDown(ui::SelectionModel* selection, i32 position, ui::KeyEventArgs& e)
        {
            const Row* row = RowAt(position);
            if (row == nullptr) { return; }
            if (e.Key == ui::KeyCode::F2)
            {
                StartRename(position);
                e.Handled = true;
            }
            else if (e.Key == ui::KeyCode::Delete)
            {
                if (row->group != nullptr) { ConfirmDeleteGroup(row->group); }
                else { ConfirmDelete(SelectedInstanceIds(selection, position)); }
                e.Handled = true;
            }
        }

        void ConfirmDeleteGroup(content::Group* group)
        {
            if (group == nullptr || group->Parent() == nullptr || Context == nullptr) { return; }
            usize assetCount = 0;
            CountInstances(group, assetCount);
            String message(u8"Delete group '");
            message += group->Name();
            message += u8"' and ALL its contents (";
            AppendCount(message, assetCount);
            message += u8" asset(s))? Source files and cooked products go away; open pages close.";

            AssetsView* self = this;
            RefPtr<ui::Dialog> dialog = ui::Dialog::Confirm(u8"Delete group", message.AsView());
            dialog->OnClosed.Add(ui::Event<void(ui::Dialog*, ui::DialogResult)>::Handler{
                [self, group](ui::Dialog*, ui::DialogResult result) {
                    if (result != ui::DialogResult::OK) { return; }
                    ui::UIContext* ctx = self->Context;
                    if (ctx == nullptr) { return; }
                    // Deferred: page teardown + DB mutation never run mid-event-dispatch.
                    ctx->MutationQueueRef().QueueAction(Function<void()>{ [self, group]() {
                        self->DeleteGroupNow(group);
                    } });
                } });
            dialog->Show(Context);
        }

        // Runs from the mutation queue: close every page under the group, delete the whole
        // subtree, navigate the selection out of the dead branch.
        void DeleteGroupNow(content::Group* group)
        {
            if (m_context->Project() == nullptr) { return; }
            // Same cook gate as ImportFile (deleting instances mid-cook dangles the worker's
            // snapshotted pointers).
            if (m_cook->MutationLocked())
            {
                AssetsView* self = this;
                m_cook->RunWhenIdle(Function<void()>{ [self, group]() { self->DeleteGroupNow(group); } });
                m_context->Notify(draconic::editor::NoticeKind::Info,
                                  u8"Delete queued until the current cook finishes.");
                return;
            }
            Array<Guid> ids;
            CollectInstanceIds(group, ids);
            if (OnCloseInstancePage)
            {
                for (const Guid& id : ids) { OnCloseInstancePage(id); }
            }
            // Navigate away BEFORE the pointers die.
            for (content::Group* g = m_selectedGroup; g != nullptr; g = g->Parent())
            {
                if (g == group) { m_selectedGroup = group->Parent(); break; }
            }
            const String path = group->Path();
            if (m_context->Project()->SourceDb().DeleteGroup(*group).IsOk())
            {
                DRACONIC_LOG_INFO(u8"Assets", u8"deleted group '{}' ({} asset(s))", path, ids.Size());
                String message(u8"Deleted group '");
                message += path;
                message += u8"'.";
                m_context->SetStatus(message.AsView());
            }
            else
            {
                DRACONIC_LOG_WARNING(u8"Assets", u8"delete FAILED for group '{}'", path);
                m_context->Notify(draconic::editor::NoticeKind::Error,
                                  u8"Delete group FAILED (see console).");
            }
            ClearDefaultSceneIfGone();
            Rebuild();   // the next cook's plan sweeps the orphaned products
        }

        static void CountInstances(content::Group* group, usize& count)
        {
            count += group->Instances().Size();
            for (content::Group* child : group->Groups()) { CountInstances(child, count); }
        }

        static void CollectInstanceIds(content::Group* group, Array<Guid>& out)
        {
            for (content::Instance* instance : group->Instances()) { out.PushBack(instance->Id()); }
            for (content::Group* child : group->Groups()) { CollectInstanceIds(child, out); }
        }

        // A delete may have taken the default scene with it - clear the manifest reference
        // instead of leaving a dangling guid (the player would fail with "unresolved").
        void ClearDefaultSceneIfGone()
        {
            auto* project = m_context->Project();
            if (project == nullptr) { return; }
            if (project->Settings().defaultSceneId.IsNil()
                && project->Settings().defaultScene.IsEmpty())
            {
                return;
            }
            const bool resolves = !project->Settings().defaultSceneId.IsNil()
                ? project->SourceDb().GetInstance(project->Settings().defaultSceneId) != nullptr
                : project->SourceDb().GetInstance(project->Settings().defaultScene.AsView()) != nullptr;
            if (resolves) { return; }
            project->Settings().defaultSceneId = Guid{};
            project->Settings().defaultScene = String();
            (void)project->SaveSettings();
            DRACONIC_LOG_INFO(u8"Assets", u8"default scene was deleted - cleared it in the manifest");
        }

        static void AppendCount(String& out, usize value)
        {
            utf8char digits[20];
            usize n = 0;
            do { digits[n++] = static_cast<utf8char>('0' + (value % 10)); value /= 10; } while (value != 0);
            while (n > 0) { out.PushBack(digits[--n]); }
        }

        [[nodiscard]] content::Instance* Resolve(const Guid& id)
        {
            return (m_context->Project() != nullptr)
                ? m_context->Project()->SourceDb().GetInstance(id) : nullptr;
        }

        draconic::editor::EditorContext* m_context;      // borrowed
        draconic::editor::EditorCookService* m_cook;     // borrowed (app-owned)
        RefPtr<ui::TreeView> m_tree;
        RefPtr<ui::ListView> m_list;
        RefPtr<ui::GridView> m_grid;
        RefPtr<ui::EditText> m_filterEdit;
        RefPtr<tk::BreadcrumbBar> m_breadcrumb;
        RefPtr<ui::ToggleButton> m_listToggle;
        RefPtr<ui::ToggleButton> m_gridToggle;
        UniquePtr<TreeAdapter> m_treeAdapter;
        UniquePtr<ListAdapter> m_listAdapter;
        UniquePtr<GridAdapter> m_gridAdapter;
        Array<GroupNode> m_groups;              // pre-order snapshot (nodeId = index)
        Array<Row> m_rows;                      // content area: subgroups then instances
        Array<content::Group*> m_breadcrumbGroups;   // segment index -> group
        content::Group* m_selectedGroup = nullptr;
        String m_filter;
        bool m_gridMode = false;
        u64 m_cookRevision = ~0ull;
    };

    DRACONIC_DEFINE_OBJECT(AssetsView, "draconic::editor::app")
}
