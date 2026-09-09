// SPDX-License-Identifier: MIT
// Copyright (c) 2026-Present Robert Campbell

// Editor::App - :asset_create_dialog partition.
//
// AssetCreateDialog: the NEW-ASSET twin of the AssetPickerDialog - choose a GROUP (tree, same
// shape as the picker's) and an ASSET NAME, with live
// validation refusing an empty name or one that already exists in the chosen group. The dialog
// is TYPE-AGNOSTIC: it never creates anything itself - Create fires OnCreate(group, name) and
// the CALLER constructs its asset type there (then typically loads it by the new instance's
// guid, since guids are the open/identity currency). First consumer: the animation panel's
// Create Clip; any New-asset flow can reuse it.

module;
#include "Core/Prelude.h"
#include "Core/Reflection/Reflect.h"

export module editor.app:asset_create_dialog;

import foundation.core;
import foundation.content;
import foundation.ui;
import editor.core;

using namespace foundation::core;

export namespace editor::app
{
    namespace ui = foundation::ui;
    namespace content = foundation::content;

    class AssetCreateDialog final : public ui::Dialog
    {
        RTTI_OBJECT(AssetCreateDialog, ui::Dialog)
    public:
        /// Create confirmed: the chosen group + a VALIDATED name (non-empty, unique in the
        /// group). Fired once, before the dialog closes. The caller creates its asset type.
        Function<void(content::Group&, StringView)> OnCreate;

        AssetCreateDialog(editor::EditorContext& context, StringView title,
                          StringView namePlaceholder)
            : ui::Dialog(title), m_context(&context)
        {
            MinWidth.SetValue(420.0f);
            MinHeight.SetValue(360.0f);
            MaxWidth.SetValue(520.0f);
            MaxHeight.SetValue(460.0f);

            auto column = MakeRef<ui::FlexLayout>(MemoryAllocator());
            column->Direction = ui::Orientation::Vertical;
            column->Spacing = 6;

            // Group tree (the picker's shape: one root, expanded).
            m_treeAdapter = MakeUnique<TreeAdapter>(MemoryAllocator(), *this);
            m_tree = MakeRef<ui::TreeView>(MemoryAllocator());
            m_tree->SetItemHeight(20.0f);
            {
                AssetCreateDialog* self = this;
                m_tree->OnItemClick.Add(
                    [self](ui::TreeView::ItemClickInfo info)
                    {
                        if (info.NodeId >= 0 &&
                            info.NodeId < static_cast<i32>(self->m_groups.Size()))
                        {
                            self->m_selectedGroup =
                                self->m_groups[static_cast<usize>(info.NodeId)].group;
                            self->Validate();
                        }
                    });
                ui::LayoutStyle lp;
                lp.Width = ui::SizeSpec::Match();
                lp.FlexGrow = 1.0f;
                column->AddView(m_tree.Get(), lp);
            }

            m_nameEdit = MakeRef<ui::EditText>(MemoryAllocator());
            m_nameEdit->SetPlaceholder(namePlaceholder);
            {
                AssetCreateDialog* self = this;
                m_nameEdit->OnTextChanged.Add([self](ui::EditText*) { self->Validate(); });
                ui::LayoutStyle lp;
                lp.Width = ui::SizeSpec::Match();
                column->AddView(m_nameEdit.Get(), lp);
            }

            // Validation line: names the refusal ("name exists in this group") - the Create
            // button alone going dead would be a silent no.
            m_validationLabel = MakeRef<ui::Label>(MemoryAllocator(), StringView(u8""));
            m_validationLabel->FontSize.SetValue(11.0f);
            {
                ui::LayoutStyle lp;
                lp.Width = ui::SizeSpec::Match();
                column->AddView(m_validationLabel.Get(), lp);
            }
            SetContent(column.Get());

            {
                AssetCreateDialog* self = this;
                m_createButton = AddButton(u8"Create", ui::DialogResult::None);
                m_createButton->OnClick.Add(
                    [self](ui::ButtonBase*)
                    {
                        if (!self->IsValid())
                        {
                            return; // the validation line says why
                        }
                        if (self->OnCreate && self->m_selectedGroup != nullptr)
                        {
                            self->OnCreate(*self->m_selectedGroup,
                                           self->m_nameEdit->Text());
                        }
                        self->Close(ui::DialogResult::OK);
                    });
                AddButton(u8"Cancel", ui::DialogResult::Cancel);
            }

            RebuildModel();
            Validate();
        }

        ~AssetCreateDialog() override { m_tree->SetAdapter(nullptr); }

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
            explicit TreeAdapter(AssetCreateDialog& owner) : m_owner(&owner) {}
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
                auto row = MakeRef<ui::FlexLayout>(m_owner->MemoryAllocator());
                auto label = MakeRef<ui::Label>(m_owner->MemoryAllocator());
                label->FontSize.SetValue(12.0f);
                ui::LayoutStyle grow;
                grow.FlexGrow = 1.0f;
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
            AssetCreateDialog* m_owner;
        };

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

        void RebuildModel()
        {
            m_groups.Clear();
            content::Group* root = (m_context->Project() != nullptr)
                                       ? m_context->Project()->SourceDb().RootGroup()
                                       : nullptr;
            if (root != nullptr)
            {
                AddGroupNode(root, 0);
            }
            if (m_selectedGroup == nullptr)
            {
                m_selectedGroup = root;
            }
            m_tree->SetAdapter(m_treeAdapter.Get());
            ui::FlattenedTreeAdapter* flat = m_tree->FlatAdapter();
            for (usize i = 0; i < m_groups.Size(); ++i)
            {
                if (!m_groups[i].children.IsEmpty())
                {
                    flat->Expand(static_cast<i32>(i));
                }
            }
        }

        [[nodiscard]] bool IsValid() const { return m_valid; }

        void Validate()
        {
            const StringView name = m_nameEdit->Text();
            m_valid = false;
            String message;
            if (m_selectedGroup == nullptr)
            {
                message = String(u8"no group selected");
            }
            else if (name.IsEmpty())
            {
                message = String(u8"enter a name");
            }
            else
            {
                bool exists = false;
                for (content::Instance* instance : m_selectedGroup->Instances())
                {
                    if (instance->Name() == name)
                    {
                        exists = true;
                        break;
                    }
                }
                if (exists)
                {
                    message = String(u8"'");
                    message += name;
                    message += u8"' already exists in this group";
                }
                else
                {
                    m_valid = true;
                    const StringView groupName = m_selectedGroup->Name();
                    message = String(u8"create in ");
                    message += groupName.IsEmpty() ? StringView(u8"Content") : groupName;
                }
            }
            m_validationLabel->SetText(message.AsView());
        }

        editor::EditorContext* m_context; // borrowed
        RefPtr<ui::TreeView> m_tree;
        RefPtr<ui::EditText> m_nameEdit;
        RefPtr<ui::Label> m_validationLabel;
        ui::Button* m_createButton = nullptr;
        UniquePtr<TreeAdapter> m_treeAdapter;
        Array<GroupNode> m_groups;
        content::Group* m_selectedGroup = nullptr;
        bool m_valid = false;
    };

    RTTI_DEFINE_OBJECT(AssetCreateDialog, "rtti::editor")
}
