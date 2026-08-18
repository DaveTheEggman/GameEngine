// Editor::App - :group_picker_dialog partition.
//
// GroupPickerDialog: the AssetPickerDialog's sibling for CONTENT GROUPS - a modal picker over
// the project's group hierarchy, shown as the same left-hand group TREE the asset picker uses
// (not the old flat path menu, which grew unusable once the tree got deep). Used to choose an
// import destination group; the current group is preselected.
//
//   - the tree is the content-DB group hierarchy from `root` (root shows as "Content")
//   - single-click selects a group; double-click or [Select] confirms; [Cancel]/Escape dismisses
//
// The result is delivered through OnPicked(group), fired BEFORE the dialog closes itself.
// Cancel fires nothing.

module;
#include "Core/Prelude.h"
#include "Core/Reflection/Reflect.h"

export module editor.app:group_picker_dialog;

import foundation.core;
import foundation.content;
import foundation.ui;

using namespace foundation::core;

export namespace editor::app
{
    namespace ui = foundation::ui;
    namespace content = foundation::content;

    class GroupPickerDialog final : public ui::Dialog
    {
        RTTI_OBJECT(GroupPickerDialog, ui::Dialog)
    public:
        /// The pick result: the chosen group. Fired once, before close. Cancel fires nothing.
        Function<void(content::Group*)> OnPicked;

        GroupPickerDialog(StringView title, content::Group* root, content::Group* preselect)
            : ui::Dialog(title), m_selectedGroup(preselect)
        {
            MinWidth.SetValue(420.0f);
            MinHeight.SetValue(340.0f);
            MaxWidth.SetValue(520.0f);
            MaxHeight.SetValue(420.0f);

            auto column = MakeRef<ui::FlexLayout>(DefaultAllocator());
            column->Direction = ui::Orientation::Vertical;
            column->Spacing = 6;

            m_treeAdapter = MakeUnique<TreeAdapter>(DefaultAllocator(), *this);
            m_tree = MakeRef<ui::TreeView>(DefaultAllocator());
            m_tree->SetItemHeight(20.0f);
            m_tree->SetAdapter(m_treeAdapter.Get());
            {
                GroupPickerDialog* self = this;
                m_tree->OnItemClick.Add(
                    [self](ui::TreeView::ItemClickInfo info)
                    {
                        if (info.NodeId >= 0 && info.NodeId < static_cast<i32>(self->m_groups.Size()))
                        {
                            self->m_selectedGroup =
                                self->m_groups[static_cast<usize>(info.NodeId)].group;
                            if (info.ClickCount >= 2)
                            {
                                self->Confirm();
                            }
                        }
                    });
                auto lp = MakeRef<ui::FlexLayoutParams>(DefaultAllocator());
                lp->Width = ui::SizeSpec::Match();
                lp->Grow = 1.0f;
                column->AddView(m_tree.Get(), lp);
            }
            SetContent(column.Get());

            {
                GroupPickerDialog* self = this;
                ui::Button* select = AddButton(u8"Select", ui::DialogResult::None);
                select->OnClick.Add([self](ui::ButtonBase*) { self->Confirm(); });
                AddButton(u8"Cancel", ui::DialogResult::Cancel);
            }

            if (root != nullptr)
            {
                AddGroupNode(root, 0);
            }
            if (m_selectedGroup == nullptr)
            {
                m_selectedGroup = root;
            }
            // Re-set the adapter now that m_groups is populated: SetAdapter reads RootCount(), which
            // was 0 while the model was still empty above - without this the tree shows nothing.
            // Then expand every group so the whole hierarchy is visible (mirrors AssetPickerDialog).
            m_tree->SetAdapter(m_treeAdapter.Get());
            if (ui::FlattenedTreeAdapter* flat = m_tree->FlatAdapter())
            {
                for (usize i = 0; i < m_groups.Size(); ++i)
                {
                    if (!m_groups[i].children.IsEmpty())
                    {
                        flat->Expand(static_cast<i32>(i));
                    }
                }
            }
        }

        ~GroupPickerDialog() override { m_tree->SetAdapter(nullptr); }

    private:
        struct GroupNode
        {
            content::Group* group = nullptr;
            i32 depth = 0;
            Array<i32> children;
        };

        class TreeAdapter final : public ui::ITreeAdapter
        {
        public:
            explicit TreeAdapter(GroupPickerDialog& owner) : m_owner(&owner) {}
            [[nodiscard]] i32 RootCount() const override
            {
                return m_owner->m_groups.IsEmpty() ? 0 : 1;
            }
            [[nodiscard]] i32 GetChildCount(i32 nodeId) const override
            {
                if (nodeId == -1)
                {
                    return RootCount();
                }
                return InRange(nodeId) ? static_cast<i32>(Node(nodeId).children.Size()) : 0;
            }
            [[nodiscard]] i32 GetChildId(i32 parentId, i32 childIndex) const override
            {
                if (parentId == -1)
                {
                    return childIndex == 0 && !m_owner->m_groups.IsEmpty() ? 0 : -1;
                }
                if (!InRange(parentId))
                {
                    return -1;
                }
                const Array<i32>& kids = Node(parentId).children;
                return (childIndex >= 0 && childIndex < static_cast<i32>(kids.Size()))
                           ? kids[static_cast<usize>(childIndex)]
                           : -1;
            }
            [[nodiscard]] i32 GetDepth(i32 nodeId) const override
            {
                return InRange(nodeId) ? Node(nodeId).depth : 0;
            }
            [[nodiscard]] bool HasChildren(i32 nodeId) const override
            {
                return GetChildCount(nodeId) > 0;
            }
            [[nodiscard]] RefPtr<ui::View> CreateView(i32) override
            {
                auto row = MakeRef<ui::FlexLayout>(DefaultAllocator());
                auto label = MakeRef<ui::Label>(DefaultAllocator());
                label->FontSize.SetValue(12.0f);
                auto grow = MakeRef<ui::FlexLayoutParams>(DefaultAllocator());
                grow->Grow = 1.0f;
                row->AddView(label.Get(), grow);
                return RefPtr<ui::View>(row.Get());
            }
            void BindView(ui::View* view, i32 nodeId, i32 depth, bool) override
            {
                auto* row = Cast<ui::FlexLayout>(view);
                if (row == nullptr || row->ChildCount() == 0 || !InRange(nodeId))
                {
                    return;
                }
                auto* label = Cast<ui::Label>(row->GetChildAt(0));
                const StringView name = Node(nodeId).group->Name();
                label->SetText(name.IsEmpty() ? StringView(u8"Content") : name);
                row->Padding = ui::Thickness{m_owner->m_tree->ContentInset(depth), 0, 0, 0};
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
            GroupPickerDialog* m_owner;
        };

        // Depth-first build of the node table (parent before children); returns the new node id.
        i32 AddGroupNode(content::Group* group, i32 depth)
        {
            const i32 id = static_cast<i32>(m_groups.Size());
            m_groups.PushBack(GroupNode{group, depth, {}});
            for (content::Group* child : group->Groups())
            {
                const i32 childId = AddGroupNode(child, depth + 1);
                m_groups[static_cast<usize>(id)].children.PushBack(childId);
            }
            return id;
        }

        void Confirm()
        {
            if (OnPicked)
            {
                OnPicked(m_selectedGroup);
            }
            Close(ui::DialogResult::OK);
        }

        RefPtr<ui::TreeView> m_tree;
        UniquePtr<TreeAdapter> m_treeAdapter;
        Array<GroupNode> m_groups;
        content::Group* m_selectedGroup = nullptr;
    };

    RTTI_DEFINE_OBJECT(GroupPickerDialog, "rtti::editor::editor::app")
}
