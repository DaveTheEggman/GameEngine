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
//   - drag a row INTO another = reparent; drag to a row EDGE = sibling reorder (insert-before
//     boundary, incl. top of the list and end-of-root-list below the last row);
//   - a header row holds [+] (create root entity - always reachable even when rows fill the
//     pane and swallow every right-click) and a filter box (matches keep their ancestors).

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
        /// Cross-page clipboard home (optional - Copy/Paste menu items appear when set).
        void SetEditorContext(EditorContext* context) noexcept { m_editor = context; }

        /// Prefab hooks (wired by the scene page - asset creation/picking lives there):
        /// turn an entity's subtree into a prefab asset + instance, and spawn an instance
        /// under `parent` (nil = scene root).
        Function<void(const Guid&)> OnCreatePrefab;
        Function<void(const Guid&)> OnSpawnPrefab;
        Function<void(const Guid&)> OnApplyPrefab;    // instance root -> write back to the asset
        Function<void(const Guid&)> OnRevertPrefab;   // instance root -> discard deltas

        explicit SceneHierarchyView(SceneEditContext& edit) : m_edit(&edit)
        {
            auto column = MakeRef<ui::FlexLayout>(DefaultAllocator());
            column->Direction = ui::Orientation::Vertical;

            // Header: [+] create root entity | filter box.
            auto header = MakeRef<ui::FlexLayout>(DefaultAllocator());
            header->Direction = ui::Orientation::Horizontal;
            header->Spacing = 4.0f;
            header->Padding = ui::Thickness{ 4, 3 };
            auto addButton = MakeRef<ui::Button>(DefaultAllocator(), StringView(u8"+"));
            {
                SceneEditContext* editPtr = m_edit;
                addButton->OnClick.Add([editPtr](ui::ButtonBase*) { (void)editPtr->CreateEntity(u8"Entity"); });
                header->AddView(addButton.Get());
            }
            m_filterEdit = MakeRef<ui::EditText>(DefaultAllocator());
            m_filterEdit->SetPlaceholder(u8"Filter...");
            {
                auto grow = MakeRef<ui::FlexLayoutParams>(DefaultAllocator());
                grow->Grow = 1.0f;
                header->AddView(m_filterEdit.Get(), grow);
            }
            column->AddView(header.Get());

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
                SceneHierarchyView* self = this;
                menu->AddItem(u8"Create Entity", [edit]() { (void)edit->CreateEntity(u8"Entity"); });
                menu->AddItem(u8"Spawn Prefab...", [self]() {
                    if (self->OnSpawnPrefab) { self->OnSpawnPrefab(Guid{}); }
                });
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
            void Bind(const Guid& entity, StringView name, i32 depth, bool prefabRoot)
            {
                m_entity = entity;
                SetText(name);
                TextOffsetX.SetValue(static_cast<f32>(depth + 1) * 20.0f);
                // Prefab-instance roots read distinctly (the Unity-blue convention); the text
                // itself stays clean so in-place renames never absorb a marker.
                if (prefabRoot) { TextColor.SetValue(Color{ 0.45f, 0.72f, 1.0f, 1.0f }); }
                else { TextColor.SetValue(Optional<Color>{}); }
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
                row->FontSize.SetValue(Optional<f32>{ 12.0f });   // match the inspector's dense 12px text
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
                const bool prefabRoot =
                    m_owner->m_edit->Scene().FindPrefabInstanceByRoot(node.id) != nullptr;
                static_cast<Row*>(view)->Bind(node.id, node.name.AsView(), depth, prefabRoot);
            }

            // Between-rows reorder: `toPosition` is the insert-before BOUNDARY (0..count;
            // count = end of the root list). Maps to a MoveEntityBefore command.
            [[nodiscard]] bool CanMove(i32 fromPosition, i32 toPosition) override
            {
                const Guid from = m_owner->GuidAtFlat(fromPosition);
                if (from == Guid{}) { return false; }
                if (toPosition >= m_owner->FlatCount()) { return true; }   // end of root list
                const Guid before = m_owner->GuidAtFlat(toPosition);
                if (before == Guid{} || before == from) { return false; }
                // Cycle: the slot's parent lies inside the moved entity's subtree.
                const dscene::EntityHandle parent =
                    m_owner->m_edit->Scene().GetParent(m_owner->m_edit->Resolve(before));
                if (parent.IsAssigned()
                    && m_owner->m_edit->IsSelfOrAncestor(m_owner->m_edit->Scene().GetEntityId(parent), from))
                {
                    return false;
                }
                return true;
            }
            void MoveItem(i32 fromPosition, i32 toPosition) override
            {
                const Guid from = m_owner->GuidAtFlat(fromPosition);
                if (from == Guid{}) { return; }
                const Guid before = (toPosition < m_owner->FlatCount())
                    ? m_owner->GuidAtFlat(toPosition) : Guid{};
                m_owner->m_edit->MoveEntityBefore(from, before);
            }

            // Drop-INTO reparents (cycle-guarded here for the hover feedback; the command
            // re-checks on execute).
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
                menu->AddItem(u8"Duplicate", [edit, id]() { (void)edit->DuplicateEntity(id); });
                menu->AddItem(u8"Create Prefab from Selection", [self, id]() {
                    if (self->OnCreatePrefab) { self->OnCreatePrefab(id); }
                });
                menu->AddItem(u8"Spawn Prefab as Child", [self, id]() {
                    if (self->OnSpawnPrefab) { self->OnSpawnPrefab(id); }
                });
                menu->AddItem(u8"Spawn Prefab at Root", [self]() {
                    if (self->OnSpawnPrefab) { self->OnSpawnPrefab(Guid{}); }
                });
                if (edit->Scene().FindPrefabInstanceByRoot(id) != nullptr)
                {
                    menu->AddSeparator();
                    menu->AddItem(u8"Apply to Prefab", [self, id]() {
                        if (self->OnApplyPrefab) { self->OnApplyPrefab(id); }
                    });
                    menu->AddItem(u8"Revert Instance", [self, id]() {
                        if (self->OnRevertPrefab) { self->OnRevertPrefab(id); }
                    });
                }
                if (EditorContext* editor = self->m_editor)
                {
                    menu->AddItem(u8"Copy", [edit, editor, id]() {
                        Array<byte> blob = edit->CopyEntity(id);
                        if (!blob.IsEmpty()) { editor->SetClipboard(u8"entities", Move(blob)); }
                    });
                    const Span<const byte> clip = editor->ClipboardData(u8"entities");
                    menu->AddItem(u8"Paste as Child", [edit, editor, id]() {
                        (void)edit->PasteEntities(editor->ClipboardData(u8"entities"), id);
                    }, !clip.IsEmpty());
                }
                menu->AddSeparator();
                menu->AddItem(u8"Delete", [edit, id]() { edit->DestroyEntity(id); });
                const Float2 screenPos = self->m_tree->InternalTreeView()->LocalToScreen(Float2{ x, y });
                menu->Show(self->Context, screenPos.x, screenPos.y);
            });

            // Right-click on empty space below the rows: the ListView consumes ALL right-clicks
            // (its contract) and routes background ones here - the OnMouseDown fallback on this
            // view never fires while the tree fills the pane.
            tree->InternalListView()->OnBackgroundRightClicked.Add([self](f32 x, f32 y) {
                if (self->Context == nullptr) { return; }
                SceneEditContext* edit = self->m_edit;
                auto menu = MakeRef<ui::ContextMenu>(DefaultAllocator());
                menu->AddItem(u8"Create Entity", [edit]() { (void)edit->CreateEntity(u8"Entity"); });
                menu->AddItem(u8"Spawn Prefab...", [self]() {
                    if (self->OnSpawnPrefab) { self->OnSpawnPrefab(Guid{}); }
                });
                if (EditorContext* editor = self->m_editor)
                {
                    const Span<const byte> clip = editor->ClipboardData(u8"entities");
                    menu->AddItem(u8"Paste", [edit, editor]() {
                        (void)edit->PasteEntities(editor->ClipboardData(u8"entities"));
                    }, !clip.IsEmpty());
                }
                const Float2 screenPos =
                    self->m_tree->InternalTreeView()->InternalListView()->LocalToScreen(Float2{ x, y });
                menu->Show(self->Context, screenPos.x, screenPos.y);
            });

            tree->OnItemKeyDown.Add([self](i32 nodeId, ui::KeyEventArgs& e) {
                const Guid id = self->GuidOfNode(nodeId);
                if (id == Guid{}) { return; }
                if (e.Key == ui::KeyCode::F2) { self->BeginRename(id); e.Handled = true; }
                else if (e.Key == ui::KeyCode::Delete) { self->m_edit->DestroyEntity(id); e.Handled = true; }
            });

            m_filterEdit->OnTextChanged.Add([self](ui::EditText* edit) {
                self->m_filter = String(edit->Text());
                self->RebuildSnapshot();   // filter changes rebuild regardless of revision
            });

            // Tree selection -> context selection is on click above; context -> tree here.
            m_edit->EntitySelection().OnChanged = [self]() {
                if (self->m_syncing) { return; }
                self->SyncSelectionToTree();
            };
        }

        // ASCII-case-insensitive substring match (v1 filter; UTF-8 folding later if needed).
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

        // True if the entity or ANY descendant matches (so ancestors of matches stay visible).
        [[nodiscard]] bool SubtreeMatches(dscene::Scene& scene, dscene::EntityHandle e) const
        {
            if (MatchesFilter(scene.GetEntityName(e), m_filter.AsView())) { return true; }
            for (dscene::EntityHandle c = scene.GetFirstChild(e); c.IsAssigned();
                 c = scene.GetNextSibling(c))
            {
                if (SubtreeMatches(scene, c)) { return true; }
            }
            return false;
        }

        // Collapse state is keyed by entity Guid so it survives rebuilds: before the snapshot
        // is thrown away, fold the current expand state into m_collapsed (entities absent from
        // the snapshot - e.g. filtered out - keep their remembered state).
        void CaptureCollapseState()
        {
            ui::FlattenedTreeAdapter* flat = m_tree->InternalTreeView()->FlatAdapter();
            if (flat == nullptr) { return; }
            for (usize i = 0; i < m_nodes.Size(); ++i)
            {
                if (m_nodes[i].children.IsEmpty()) { continue; }
                if (flat->IsExpanded(static_cast<i32>(i))) { m_collapsed.Remove(m_nodes[i].id); }
                else { m_collapsed.Insert(m_nodes[i].id); }
            }
        }

        void RebuildSnapshot()
        {
            CaptureCollapseState();
            m_nodes.Clear();
            m_roots.Clear();
            dscene::Scene& scene = m_edit->Scene();

            // Roots in LIST order (the order reorder edits maintain and serialization
            // preserves), then depth-first children. With a filter, keep nodes whose subtree
            // contains a match.
            for (dscene::EntityHandle r = scene.GetFirstRoot(); r.IsAssigned();
                 r = scene.GetNextSibling(r))
            {
                if (SubtreeMatches(scene, r)) { m_roots.PushBack(AddNode(scene, r, 0)); }
            }

            // Rebuild the flat view (SetAdapter recreates the flattened tree). New entities
            // default to expanded so structural edits stay visible; entities the user collapsed
            // stay collapsed (state captured above, keyed by Guid).
            m_tree->SetAdapter(m_adapter.Get());
            ui::FlattenedTreeAdapter* flat = m_tree->InternalTreeView()->FlatAdapter();
            for (usize i = 0; i < m_nodes.Size(); ++i)
            {
                if (m_nodes[i].children.IsEmpty()) { continue; }
                if (!m_collapsed.Contains(m_nodes[i].id)) { flat->Expand(static_cast<i32>(i)); }
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
                if (!SubtreeMatches(scene, c)) { continue; }
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

        [[nodiscard]] i32 FlatCount() const
        {
            ui::FlattenedTreeAdapter* flat = m_tree->InternalTreeView()->FlatAdapter();
            return (flat != nullptr) ? flat->ItemCount() : 0;
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

        SceneEditContext* m_edit;            // borrowed (the page owns it)
        EditorContext* m_editor = nullptr;   // borrowed; clipboard home (optional)
        RefPtr<tk::DraggableTreeView> m_tree;
        RefPtr<ui::EditText> m_filterEdit;
        UniquePtr<Adapter> m_adapter;
        Array<Node> m_nodes;    // pre-order snapshot of the scene (nodeId = index)
        Array<i32> m_roots;
        HashSet<Guid> m_collapsed;   // entities the user collapsed (survives rebuilds)
        u64 m_revision = ~0ull;
        String m_filter;
        bool m_syncing = false;
    };

    DRACONIC_DEFINE_OBJECT(SceneHierarchyView, "draconic::editor")
}
