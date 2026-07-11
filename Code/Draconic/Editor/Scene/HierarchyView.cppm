// Draconic::EditorScene - :hierarchy partition.
//
// SceneHierarchyView: the entity tree INSIDE a scene page (multi-scene rule - one per page,
// never a global panel; §3.6). A DraggableTreeView over a rebuilt snapshot of the live scene
// (Scene::Revision() gates the rebuild, so command execute/undo/redo all refresh it for free),
// wired to the page's SceneEditContext:
//   - click selects (per-page Guid selection, synced both ways with the tree's SelectionModel);
//   - right-click context menu: Create Child / Rename / Delete on rows, Create Entity on empty;
//   - rows are EditableLabels: double-click / slow-click renames in place (single clicks pass
//     through to selection by design), F2 / context-menu Rename triggers the same edit,
//     Delete deletes;
//   - drag a row INTO another = reparent (the toolkit's drop-into zones; between-rows reorder
//     is refused until the Scene grows sibling-order APIs).

module;
#include "Core/Prelude.h"
#include "Core/Reflection/Reflect.h"

export module draconic.editor.scene:hierarchy;

import draconic.core;
import draconic.fonts;
import draconic.scene;
import draconic.ui;
import draconic.ui.toolkit;
import draconic.editor.core;
import :edit;

using namespace draconic::core;

export namespace draconic::editor
{
    namespace ui = draconic::ui;
    namespace tk = draconic::ui::toolkit;
    namespace dscene = draconic::scene;

    class SceneHierarchyView : public ui::ViewGroup
    {
        DRACONIC_OBJECT(SceneHierarchyView, ui::ViewGroup)
    public:
        explicit SceneHierarchyView(SceneEditContext& edit) : m_edit(&edit)
        {
            auto column = MakeRef<ui::FlexLayout>(DefaultAllocator());
            column->Direction = ui::Orientation::Vertical;

            m_adapter = MakeUnique<Adapter>(DefaultAllocator(), *this);
            m_tree = MakeRef<tk::DraggableTreeView>(DefaultAllocator());
            m_tree->SetItemHeight(22.0f);
            m_tree->SetAdapter(m_adapter.Get());
            {
                auto grow = MakeRef<ui::FlexLayoutParams>(DefaultAllocator());
                grow->Grow = 1.0f;
                column->AddView(m_tree.Get(), grow);
            }

            AddView(column.Get());

            WireEvents();
        }

        /// Per-frame: rebuild the snapshot when the scene changed, keep selection in sync.
        void Refresh()
        {
            if (m_edit->Scene().Revision() != m_revision)
            {
                m_revision = m_edit->Scene().Revision();
                RebuildSnapshot();
            }
        }

        /// Begin the in-place rename of an entity (F2 / context menu; double-click and
        /// slow-click on the row do the same via the EditableLabel itself).
        void BeginRename(const Guid& entity)
        {
            ui::FlattenedTreeAdapter* flat = m_tree->InternalTreeView()->FlatAdapter();
            if (flat == nullptr) { return; }
            for (i32 pos = 0; pos < flat->ItemCount(); ++pos)
            {
                if (GuidOfNode(flat->GetNodeId(pos)) == entity)
                {
                    m_tree->InternalTreeView()->InternalListView()->ScrollToPosition(pos);
                    if (auto* row = static_cast<Row*>(
                            m_tree->InternalTreeView()->InternalListView()->GetActiveView(pos)))
                    {
                        row->BeginEdit();
                    }
                    return;
                }
            }
        }

        [[nodiscard]] tk::DraggableTreeView* Tree() const noexcept { return m_tree.Get(); }
        [[nodiscard]] usize NodeCount() const noexcept { return m_nodes.Size(); }

        // Right-click on empty space (below the rows): create a root entity.
        void OnMouseDown(ui::MouseEventArgs& e) override
        {
            if (e.Button == ui::MouseButton::Right && Context != nullptr)
            {
                auto menu = MakeRef<ui::ContextMenu>(DefaultAllocator());
                SceneEditContext* edit = m_edit;
                menu->AddItem(u8"Create Entity", [edit]() { (void)edit->CreateEntity(u8"Entity"); });
                const Float2 screenPos = LocalToScreen(Float2{ e.X, e.Y });
                menu->Show(Context, screenPos.x, screenPos.y);
                e.Handled = true;
            }
        }

        // Fill the available space (wrap-to-children would fight the virtualized tree).
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
        struct Node
        {
            Guid id;
            String name;
            i32 depth = 0;
            Array<i32> children;
        };

        // A row IS an EditableLabel (depth-indented via TextOffsetX): double-click / slow-click
        // edits in place, single clicks deliberately pass through to the list's selection, and
        // Enter/Escape commit/cancel. The adapter created it, so static_cast recovery is safe.
        class Row final : public ui::EditableLabel
        {
        public:
            void Bind(const Guid& entity, StringView name, i32 depth)
            {
                m_entity = entity;
                SetText(name);
                TextOffsetX.SetValue(static_cast<f32>(depth + 1) * 20.0f);
            }
            [[nodiscard]] const Guid& Entity() const noexcept { return m_entity; }
        private:
            Guid m_entity;
        };

        class Adapter final : public tk::IReorderableTreeAdapter
        {
        public:
            explicit Adapter(SceneHierarchyView& owner) : m_owner(&owner) {}

            [[nodiscard]] i32 RootCount() const override { return static_cast<i32>(m_owner->m_roots.Size()); }
            [[nodiscard]] i32 GetChildCount(i32 nodeId) const override
            {
                if (nodeId == -1) { return RootCount(); }
                return InRange(nodeId) ? static_cast<i32>(m_owner->m_nodes[static_cast<usize>(nodeId)].children.Size()) : 0;
            }
            [[nodiscard]] i32 GetChildId(i32 parentId, i32 childIndex) const override
            {
                if (parentId == -1)
                {
                    return (childIndex >= 0 && childIndex < RootCount())
                        ? m_owner->m_roots[static_cast<usize>(childIndex)] : -1;
                }
                if (!InRange(parentId)) { return -1; }
                const Array<i32>& kids = m_owner->m_nodes[static_cast<usize>(parentId)].children;
                return (childIndex >= 0 && childIndex < static_cast<i32>(kids.Size()))
                    ? kids[static_cast<usize>(childIndex)] : -1;
            }
            [[nodiscard]] i32 GetDepth(i32 nodeId) const override
            {
                return InRange(nodeId) ? m_owner->m_nodes[static_cast<usize>(nodeId)].depth : 0;
            }
            [[nodiscard]] bool HasChildren(i32 nodeId) const override { return GetChildCount(nodeId) > 0; }
            [[nodiscard]] RefPtr<ui::View> CreateView(i32) override
            {
                auto row = MakeRef<Row>(DefaultAllocator());
                SceneEditContext* edit = m_owner->m_edit;
                Row* raw = row.Get();
                row->OnRenameCommitted.Add([edit, raw](ui::EditableLabel*, StringView newName) {
                    edit->RenameEntity(raw->Entity(), newName);
                });
                return RefPtr<ui::View>(row.Get());
            }
            void BindView(ui::View* view, i32 nodeId, i32 depth, bool) override
            {
                if (!InRange(nodeId)) { return; }
                const Node& node = m_owner->m_nodes[static_cast<usize>(nodeId)];
                static_cast<Row*>(view)->Bind(node.id, node.name.AsView(), depth);
            }

            // Between-rows reorder is unsupported (Scene has no sibling-order API yet)...
            [[nodiscard]] bool CanMove(i32, i32) override { return false; }
            void MoveItem(i32, i32) override {}

            // ...but drop-INTO reparents (cycle-guarded here for the hover feedback; the
            // command re-checks on execute).
            [[nodiscard]] bool CanDropInto(i32 fromPosition, i32 toPosition) override
            {
                const Guid from = m_owner->GuidAtFlat(fromPosition);
                const Guid to = m_owner->GuidAtFlat(toPosition);
                if (from == Guid{} || to == Guid{} || from == to) { return false; }
                return !m_owner->m_edit->IsSelfOrAncestor(to, from);   // target under source = cycle
            }
            void DropInto(i32 fromPosition, i32 toPosition) override
            {
                const Guid from = m_owner->GuidAtFlat(fromPosition);
                const Guid to = m_owner->GuidAtFlat(toPosition);
                if (from != Guid{} && to != Guid{}) { m_owner->m_edit->ReparentEntity(from, to); }
            }

        private:
            [[nodiscard]] bool InRange(i32 nodeId) const
            {
                return nodeId >= 0 && nodeId < static_cast<i32>(m_owner->m_nodes.Size());
            }
            SceneHierarchyView* m_owner;
        };

        void WireEvents()
        {
            ui::TreeView* tree = m_tree->InternalTreeView();
            SceneHierarchyView* self = this;

            tree->OnItemClick.Add([self](ui::TreeView::ItemClickInfo info) {
                if (self->m_syncing) { return; }
                const Guid id = self->GuidOfNode(info.NodeId);
                if (id != Guid{})
                {
                    self->m_syncing = true;
                    self->m_edit->EntitySelection().Set(id);
                    self->m_syncing = false;
                }
            });

            tree->OnItemRightClick.Add([self](i32 nodeId, f32 x, f32 y) {
                const Guid id = self->GuidOfNode(nodeId);
                if (id == Guid{} || self->Context == nullptr) { return; }
                self->m_edit->EntitySelection().Set(id);

                SceneEditContext* edit = self->m_edit;
                auto menu = MakeRef<ui::ContextMenu>(DefaultAllocator());
                menu->AddItem(u8"Create Child", [edit, id]() { (void)edit->CreateEntity(u8"Entity", id); });
                menu->AddItem(u8"Rename", [self, id]() { self->BeginRename(id); });
                menu->AddSeparator();
                menu->AddItem(u8"Delete", [edit, id]() { edit->DestroyEntity(id); });
                const Float2 screenPos = self->m_tree->InternalTreeView()->LocalToScreen(Float2{ x, y });
                menu->Show(self->Context, screenPos.x, screenPos.y);
            });

            tree->OnItemKeyDown.Add([self](i32 nodeId, ui::KeyEventArgs& e) {
                const Guid id = self->GuidOfNode(nodeId);
                if (id == Guid{}) { return; }
                if (e.Key == ui::KeyCode::F2) { self->BeginRename(id); e.Handled = true; }
                else if (e.Key == ui::KeyCode::Delete) { self->m_edit->DestroyEntity(id); e.Handled = true; }
            });

            // Tree selection -> context selection is on click above; context -> tree here.
            m_edit->EntitySelection().OnChanged = [self]() {
                if (self->m_syncing) { return; }
                self->SyncSelectionToTree();
            };
        }

        void RebuildSnapshot()
        {
            m_nodes.Clear();
            m_roots.Clear();
            dscene::Scene& scene = m_edit->Scene();

            // Roots first (scene iteration order), then depth-first children.
            scene.ForEachEntity([this, &scene](dscene::EntityHandle e) {
                if (!scene.GetParent(e).IsAssigned())
                {
                    m_roots.PushBack(AddNode(scene, e, 0));
                }
            });

            // Rebuild the flat view (SetAdapter recreates the flattened tree), expanded by
            // default so structural edits stay visible (per-rebuild collapse is v1-lossy).
            m_tree->SetAdapter(m_adapter.Get());
            ui::FlattenedTreeAdapter* flat = m_tree->InternalTreeView()->FlatAdapter();
            for (usize i = 0; i < m_nodes.Size(); ++i)
            {
                if (!m_nodes[i].children.IsEmpty()) { flat->Expand(static_cast<i32>(i)); }
            }
            m_tree->InternalTreeView()->InternalListView()->NotifyDataChanged();
            SyncSelectionToTree();
        }

        i32 AddNode(dscene::Scene& scene, dscene::EntityHandle e, i32 depth)
        {
            const i32 nodeId = static_cast<i32>(m_nodes.Size());
            Node node;
            node.id = scene.GetEntityId(e);
            node.name = String(scene.GetEntityName(e));
            if (node.name.IsEmpty()) { node.name = String(u8"(unnamed)"); }
            node.depth = depth;
            m_nodes.PushBack(Move(node));

            for (dscene::EntityHandle c = scene.GetFirstChild(e); c.IsAssigned();
                 c = scene.GetNextSibling(c))
            {
                const i32 child = AddNode(scene, c, depth + 1);
                m_nodes[static_cast<usize>(nodeId)].children.PushBack(child);
            }
            return nodeId;
        }

        [[nodiscard]] Guid GuidOfNode(i32 nodeId) const
        {
            return (nodeId >= 0 && nodeId < static_cast<i32>(m_nodes.Size()))
                ? m_nodes[static_cast<usize>(nodeId)].id : Guid{};
        }

        [[nodiscard]] Guid GuidAtFlat(i32 flatPosition) const
        {
            ui::FlattenedTreeAdapter* flat = m_tree->InternalTreeView()->FlatAdapter();
            return (flat != nullptr) ? GuidOfNode(flat->GetNodeId(flatPosition)) : Guid{};
        }

        void SyncSelectionToTree()
        {
            const Guid* primary = m_edit->EntitySelection().Primary();
            ui::SelectionModel& sel = m_tree->Selection();
            m_syncing = true;
            if (primary == nullptr)
            {
                sel.ClearSelection();
            }
            else if (ui::FlattenedTreeAdapter* flat = m_tree->InternalTreeView()->FlatAdapter())
            {
                for (i32 pos = 0; pos < flat->ItemCount(); ++pos)
                {
                    if (GuidOfNode(flat->GetNodeId(pos)) == *primary)
                    {
                        sel.Select(pos);
                        break;
                    }
                }
            }
            m_syncing = false;
        }

        SceneEditContext* m_edit;   // borrowed (the page owns it)
        RefPtr<tk::DraggableTreeView> m_tree;
        UniquePtr<Adapter> m_adapter;
        Array<Node> m_nodes;    // pre-order snapshot of the scene (nodeId = index)
        Array<i32> m_roots;
        u64 m_revision = ~0ull;
        bool m_syncing = false;
    };

    DRACONIC_DEFINE_OBJECT(SceneHierarchyView, "draconic::editor")
}
