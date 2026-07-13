// Draconic::EditorApp - :assets_view partition.
//
// AssetsView: the Assets panel (asset-pipeline design §7) - source-DB-backed, the typed DB is
// the truth (Traktor's DatabaseView model, not a directory scan). Left = group tree; right =
// [breadcrumb | list/grid toggle] over [filter] over the selected group's content. The content
// area shows SUBGROUPS first (double-click descends; the breadcrumb climbs back up), then
// instances; a non-empty filter searches instances across ALL groups. Rows show
// "name - Type [badge]" where the badge is the cook state (cooked/missing/FAILED; builder-less
// types like scenes show none). Interactions:
//   - double-click: descend into a group / open the instance's editor page
//   - right-click row: Open / Duplicate / Cook / Rebuild / Delete (multi-select aware:
//     Ctrl/Shift extend the selection; Delete acts on every selected instance)
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
            }

            // Right: [breadcrumb | view toggle] over [filter] over [list | grid].
            auto right = MakeRef<ui::FlexLayout>(DefaultAllocator());
            right->Direction = ui::Orientation::Vertical;
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
        /// registered importers. Reports through the context status line.
        void ImportFile(StringView path)
        {
            if (m_context->Project() == nullptr) { return; }
            const String ext = draconic::editor::FileExtensionLower(path);
            draconic::editor::IFileImporter* importer = m_context->Importers().FindFor(ext.AsView());
            if (importer == nullptr)
            {
                String message(u8"No importer for '");
                message += draconic::editor::FileNameOf(path);
                message += u8"'.";
                m_context->SetStatus(message.AsView());
                return;
            }
            content::Group* group = (m_selectedGroup != nullptr)
                ? m_selectedGroup : m_context->Project()->SourceDb().RootGroup();
            Result<content::Instance*> imported =
                importer->Import(path, *m_context->Project(), *group);
            if (imported.HasValue() && imported.Value() != nullptr)
            {
                String message(u8"Imported '");
                message += imported.Value()->Name();
                message += u8"' (";
                message += importer->Label();
                message += u8").";
                m_context->SetStatus(message.AsView());
            }
            else
            {
                m_context->SetStatus(u8"Import FAILED (see console).");
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
                auto row = MakeRef<ui::FlexLayout>(DefaultAllocator());
                auto label = MakeRef<ui::Label>(DefaultAllocator());
                label->FontSize.SetValue(13.0f);
                auto grow = MakeRef<ui::FlexLayoutParams>(DefaultAllocator());
                grow->Grow = 1.0f;
                row->AddView(label.Get(), grow);
                return RefPtr<ui::View>(row.Get());
            }
            void BindView(ui::View* view, i32 nodeId, i32 depth, bool) override
            {
                auto* row = Cast<ui::FlexLayout>(view);
                if (row == nullptr || row->ChildCount() == 0 || !InRange(nodeId)) { return; }
                auto* label = Cast<ui::Label>(row->GetChildAt(0));
                const GroupNode& node = Node(nodeId);
                const StringView name = node.group->Name();
                label->SetText(name.IsEmpty() ? StringView(u8"Content") : name);
                row->Padding = ui::Thickness{ static_cast<f32>(depth + 1) * 18.0f, 0, 0, 0 };
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
                auto label = MakeRef<ui::Label>(DefaultAllocator());
                label->FontSize.SetValue(13.0f);
                auto grow = MakeRef<ui::FlexLayoutParams>(DefaultAllocator());
                grow->Grow = 1.0f;
                row->AddView(label.Get(), grow);
                return RefPtr<ui::View>(row.Get());
            }
            void BindView(ui::View* view, i32 position) override
            {
                auto* row = Cast<ui::FlexLayout>(view);
                if (row == nullptr || row->ChildCount() < 2) { return; }
                auto* iconView = Cast<ui::DrawableView>(row->GetChildAt(0));
                auto* label = Cast<ui::Label>(row->GetChildAt(1));
                if (iconView == nullptr || label == nullptr) { return; }
                iconView->Drawable = ui::DrawablePtr(m_owner->RowIcon(position));
                String text;
                Color color{ 0.85f, 0.85f, 0.85f, 1.0f };
                m_owner->RowText(position, text, color);
                label->SetText(text.AsView());
                label->TextColor.SetValue(Optional<Color>(color));
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
                auto iconView = MakeRef<ui::DrawableView>(DefaultAllocator());
                iconView->DesiredWidth.SetValue(Optional<f32>(40.0f));
                iconView->DesiredHeight.SetValue(Optional<f32>(40.0f));
                iconRow->AddView(iconView.Get());
                auto name = MakeRef<ui::Label>(DefaultAllocator());
                name->FontSize.SetValue(12.0f);
                name->HAlign.SetValue(draconic::fonts::TextAlignment::Center);
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
                auto* name = Cast<ui::Label>(tile->GetChildAt(1));
                if (iconRow == nullptr || iconRow->ChildCount() < 1 || name == nullptr) { return; }
                auto* iconView = Cast<ui::DrawableView>(iconRow->GetChildAt(0));
                const Row* row = m_owner->RowAt(position);
                if (iconView == nullptr || row == nullptr) { return; }
                iconView->Drawable = ui::DrawablePtr(m_owner->RowIcon(position));
                if (row->group != nullptr)
                {
                    name->SetText(row->group->Name());
                    name->TextColor.SetValue(Optional<Color>(Color{ 0.85f, 0.75f, 0.5f, 1.0f }));
                    return;
                }
                content::Instance* instance = m_owner->Resolve(row->id);
                if (instance == nullptr) { return; }
                // The name carries the cook badge color (the tile has no badge text).
                Color color{ 0.85f, 0.85f, 0.85f, 1.0f };
                switch (m_owner->m_cook->BadgeFor(*instance))
                {
                    case draconic::editor::CookBadge::Cooked:  color = Color{ 0.6f, 0.9f, 0.6f, 1.0f }; break;
                    case draconic::editor::CookBadge::Missing: color = Color{ 0.95f, 0.85f, 0.5f, 1.0f }; break;
                    case draconic::editor::CookBadge::Failed:  color = Color{ 1.0f, 0.45f, 0.45f, 1.0f }; break;
                    case draconic::editor::CookBadge::NoBuilder: break;
                }
                String tileName;
                if (m_owner->m_context->IsFavorite(row->id)) { tileName.Append(u8"* "); }
                tileName.Append(instance->Name());
                name->SetText(tileName.AsView());
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

        void RowText(i32 position, String& text, Color& color)
        {
            const Row* row = RowAt(position);
            if (row == nullptr) { return; }
            if (row->group != nullptr)
            {
                text.Append(u8"[");
                text.Append(row->group->Name());
                text.Append(u8"]");
                color = Color{ 0.85f, 0.75f, 0.5f, 1.0f };
                return;
            }
            content::Instance* instance = Resolve(row->id);
            if (instance == nullptr) { return; }
            if (m_context->IsFavorite(row->id)) { text.Append(u8"* "); }
            text.Append(instance->Name());
            text.Append(u8"   -   ");
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

            // Group rows: navigation only.
            if (row->group != nullptr)
            {
                content::Group* group = row->group;
                auto menu = MakeRef<ui::ContextMenu>(DefaultAllocator());
                menu->AddItem(u8"Open", [self, group]() { self->SelectGroup(group); });
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
            menu->AddItem(u8"Duplicate", [self, id]() { self->DuplicateInstance(id); });
            menu->AddItem(m_context->IsFavorite(id) ? StringView(u8"Unpin favorite")
                                                    : StringView(u8"Pin favorite"),
                          [self, id]() {
                              self->m_context->ToggleFavorite(id);
                              self->RebuildList();
                          });
            menu->AddSeparator();
            menu->AddItem(u8"Cook", [self]() { self->m_cook->RequestCook(false); });
            menu->AddItem(u8"Rebuild All", [self]() { self->m_cook->RequestCook(true); });
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
                m_context->SetStatus(u8"Duplicate FAILED (see console).");
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
            Rebuild();   // the next cook's plan sweeps the orphaned products
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
