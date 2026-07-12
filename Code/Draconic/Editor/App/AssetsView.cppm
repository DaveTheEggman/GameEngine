// Draconic::EditorApp - :assets_view partition.
//
// AssetsView: the Assets panel (asset-pipeline design §7) - source-DB-backed, the typed DB is
// the truth (Traktor's DatabaseView model, not a directory scan). Left = group tree; right =
// filter box over the selected group's instances (a non-empty filter searches ALL groups).
// Rows show "name - Type [badge]" where the badge is the cook state (cooked/missing/FAILED;
// builder-less types like scenes show none). Interactions:
//   - double-click: open the instance's editor page (routed by the app's page factories)
//   - right-click row: Open / Cook / Rebuild / Delete
//   - right-click background: New <creator>... / New Group / Cook All
// The view refreshes off three revisions: source-DB shape (tracked locally via Rebuild calls),
// the cook service's revision (badges after a cook), and the filter text.

module;
#include "Core/Prelude.h"
#include "Core/Reflection/Reflect.h"

export module draconic.editor.app:assets_view;

import draconic.core;
import draconic.content;
import draconic.ui;
import draconic.ui.toolkit;
import draconic.editor.core;

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
        Function<void(const draconic::editor::EditorContext::AssetCreator&)> OnCreate;

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
                        self->m_selectedGroup = self->m_groups[static_cast<usize>(info.NodeId)].group;
                        self->RebuildList();
                    }
                });
            }

            // Right: [filter] over [instance list].
            auto right = MakeRef<ui::FlexLayout>(DefaultAllocator());
            right->Direction = ui::Orientation::Vertical;
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
            m_list = MakeRef<ui::ListView>(DefaultAllocator());
            m_list->ItemHeight.SetValue(22.0f);
            m_list->SetAdapter(m_listAdapter.Get());
            {
                AssetsView* self = this;
                m_list->OnItemClicked.Add([self](i32 position, i32 clickCount, f32, f32) {
                    if (clickCount < 2) { return; }   // double-click opens
                    if (content::Instance* instance = self->InstanceAt(position))
                    {
                        if (self->OnOpenInstance) { self->OnOpenInstance(*instance); }
                    }
                });
                m_list->OnItemRightClicked.Add([self](i32 position, f32 x, f32 y) {
                    self->ShowInstanceMenu(position, x, y);
                });
                m_list->OnBackgroundRightClicked.Add([self](f32 x, f32 y) {
                    self->ShowBackgroundMenu(x, y);
                });
                auto grow = MakeRef<ui::FlexLayoutParams>(DefaultAllocator());
                grow->Grow = 1.0f;
                grow->Width = ui::SizeSpec::Match();
                right->AddView(m_list.Get(), grow);
            }

            split->SetPanes(m_tree.Get(), right.Get());
            AddView(split.Get());
            Rebuild();
        }

        ~AssetsView() override
        {
            m_tree->SetAdapter(nullptr);
            m_list->SetAdapter(nullptr);
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
            if (m_selectedGroup == nullptr) { m_selectedGroup = root; }

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
                return static_cast<i32>(m_owner->m_items.Size());
            }
            [[nodiscard]] RefPtr<ui::View> CreateView(i32) override
            {
                auto label = MakeRef<ui::Label>(DefaultAllocator());
                label->FontSize.SetValue(13.0f);
                return RefPtr<ui::View>(label.Get());
            }
            void BindView(ui::View* view, i32 position) override
            {
                auto* label = Cast<ui::Label>(view);
                content::Instance* instance = m_owner->InstanceAt(position);
                if (label == nullptr || instance == nullptr) { return; }

                String text(instance->Name());
                text.Append(u8"   -   ");
                text.Append(instance->TypeName());
                Color color{ 0.85f, 0.85f, 0.85f, 1.0f };
                switch (m_owner->m_cook->BadgeFor(*instance))
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
                label->SetText(text.AsView());
                label->TextColor.SetValue(Optional<Color>(color));
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

        void RebuildList()
        {
            m_items.Clear();
            if (m_context->Project() != nullptr)
            {
                if (m_filter.IsEmpty())
                {
                    if (m_selectedGroup != nullptr)
                    {
                        for (content::Instance* instance : m_selectedGroup->Instances())
                        {
                            m_items.PushBack(instance->Id());
                        }
                    }
                }
                else
                {
                    CollectFiltered(m_context->Project()->SourceDb().RootGroup());
                }
            }
            m_list->NotifyDataChanged();
        }

        void CollectFiltered(content::Group* group)
        {
            if (group == nullptr) { return; }
            for (content::Instance* instance : group->Instances())
            {
                if (MatchesFilter(instance->Name(), m_filter.AsView()))
                {
                    m_items.PushBack(instance->Id());
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

        [[nodiscard]] content::Instance* InstanceAt(i32 position)
        {
            if (position < 0 || position >= static_cast<i32>(m_items.Size())) { return nullptr; }
            if (m_context->Project() == nullptr) { return nullptr; }
            return m_context->Project()->SourceDb().GetInstance(m_items[static_cast<usize>(position)]);
        }

        // === menus ===

        void ShowInstanceMenu(i32 position, f32 x, f32 y)
        {
            content::Instance* instance = InstanceAt(position);
            if (instance == nullptr || Context == nullptr) { return; }
            const Guid id = instance->Id();
            AssetsView* self = this;

            auto menu = MakeRef<ui::ContextMenu>(DefaultAllocator());
            menu->AddItem(u8"Open", [self, id]() {
                if (content::Instance* inst = self->Resolve(id))
                {
                    if (self->OnOpenInstance) { self->OnOpenInstance(*inst); }
                }
            });
            menu->AddSeparator();
            menu->AddItem(u8"Cook", [self]() { self->m_cook->RequestCook(false); });
            menu->AddItem(u8"Rebuild All", [self]() { self->m_cook->RequestCook(true); });
            menu->AddSeparator();
            menu->AddItem(u8"Delete", [self, id]() {
                if (self->m_context->Project() != nullptr)
                {
                    (void)self->m_context->Project()->SourceDb().DeleteInstance(id);
                    self->Rebuild();   // the next cook's plan sweeps the orphaned product
                }
            });
            const Float2 screenPos = m_list->LocalToScreen(Float2{ x, y });
            menu->Show(Context, screenPos.x, screenPos.y);
        }

        void ShowBackgroundMenu(f32 x, f32 y)
        {
            if (Context == nullptr) { return; }
            AssetsView* self = this;
            auto menu = MakeRef<ui::ContextMenu>(DefaultAllocator());
            for (const draconic::editor::EditorContext::AssetCreator& creator : m_context->Creators())
            {
                String label(u8"New ");
                label += creator.label;
                const auto* entry = &creator;
                menu->AddItem(label.AsView(), [self, entry]() {
                    if (self->OnCreate) { self->OnCreate(*entry); }
                    self->Rebuild();
                });
            }
            if (!m_context->Creators().IsEmpty()) { menu->AddSeparator(); }
            menu->AddItem(u8"Cook All", [self]() { self->m_cook->RequestCook(false); });
            menu->AddItem(u8"Rebuild All", [self]() { self->m_cook->RequestCook(true); });
            const Float2 screenPos = m_list->LocalToScreen(Float2{ x, y });
            menu->Show(Context, screenPos.x, screenPos.y);
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
        RefPtr<ui::EditText> m_filterEdit;
        UniquePtr<TreeAdapter> m_treeAdapter;
        UniquePtr<ListAdapter> m_listAdapter;
        Array<GroupNode> m_groups;       // pre-order snapshot (nodeId = index)
        Array<Guid> m_items;             // instances shown in the list (by id: safe across deletes)
        content::Group* m_selectedGroup = nullptr;
        String m_filter;
        u64 m_cookRevision = ~0ull;
    };

    DRACONIC_DEFINE_OBJECT(AssetsView, "draconic::editor::app")
}
