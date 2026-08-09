// CollisionShapeEditorPage implementation (see CollisionShapePage.cppm).

module;
#include "Core/Prelude.h"
#include "Core/Log/Log.h"

module draconic.editor.physics;

import draconic.core;
import draconic.content;
import draconic.physics.pipeline;
import draconic.ui;
import draconic.editor.core;
import draconic.editor.app;

using namespace draconic::core;

namespace draconic::editor
{
    namespace ui = draconic::ui;

    StringView CollisionShapeEditorPage::CookLabel(draconic::pipeline::CollisionCookKind kind)
    {
        return kind == draconic::pipeline::CollisionCookKind::ConvexHull ? StringView(u8"Convex hull (dynamic)")
                                                              : StringView(u8"Triangle mesh (static)");
    }

    String CollisionShapeEditorPage::MeshName(const Guid& id) const
    {
        if (id.IsNil() || m_context->Project() == nullptr)
        {
            return String(u8"(none - pick a mesh)");
        }
        content::Instance* inst = m_context->Project()->SourceDb().GetInstance(id);
        return inst != nullptr ? String(inst->Path().AsView()) : String(u8"(missing mesh)");
    }

    static ui::FlexLayout* AddLabeledRow(ui::FlexLayout& column, StringView labelText)
    {
        auto row = MakeRef<ui::FlexLayout>(DefaultAllocator());
        row->Direction = ui::Orientation::Horizontal;
        row->Spacing = 6.0f;
        auto label = MakeRef<ui::Label>(DefaultAllocator(), labelText);
        auto llp = MakeRef<ui::FlexLayoutParams>(DefaultAllocator());
        llp->Width = ui::SizeSpec::Fixed(ui::Unit::Px(90.0f));
        llp->AlignSelf = ui::Align::Center;
        row->AddView(label.Get(), llp);
        ui::FlexLayout* raw = row.Get();
        auto lp = MakeRef<ui::FlexLayoutParams>(DefaultAllocator());
        lp->Width = ui::SizeSpec::Match();
        column.AddView(row.Get(), lp);
        return raw;
    }

    CollisionShapeEditorPage::CollisionShapeEditorPage(EditorContext& context,
                                                      content::Instance& instance)
        : m_context(&context), m_title(instance.Name())
    {
        SetInstanceId(instance.Id());
        RefPtr<ISerializable> object = instance.ReadObject();
        m_asset =
            RefPtr<draconic::pipeline::CollisionShapeAsset>(Cast<draconic::pipeline::CollisionShapeAsset>(object.Get()));
        if (m_asset.Get() == nullptr)
        {
            DRACONIC_LOG_ERROR(u8"Editor", u8"collision shape '{}' failed to read - page opens empty",
                               m_title);
        }

        auto column = MakeRef<ui::FlexLayout>(DefaultAllocator());
        column->Direction = ui::Orientation::Vertical;
        column->Spacing = 8.0f;
        column->Padding = ui::Thickness{10, 8};

        CollisionShapeEditorPage* self = this;

        // Source mesh: a typed picker (no guid string).
        {
            ui::FlexLayout* row = AddLabeledRow(*column, u8"Source mesh");
            m_meshLabel = MakeRef<ui::Label>(DefaultAllocator(), StringView(u8""));
            auto lp = MakeRef<ui::FlexLayoutParams>(DefaultAllocator());
            lp->Grow = 1.0f;
            lp->AlignSelf = ui::Align::Center;
            row->AddView(m_meshLabel.Get(), lp);
            auto pick = MakeRef<ui::Button>(DefaultAllocator(), StringView(u8"Pick..."));
            pick->OnClick.Add([self](ui::ButtonBase*) { self->PickMesh(); });
            row->AddView(pick.Get());
        }

        // Cook kind: a toggle (convex hull / triangle mesh).
        {
            ui::FlexLayout* row = AddLabeledRow(*column, u8"Cook");
            m_cookButton = MakeRef<ui::Button>(
                DefaultAllocator(),
                m_asset.Get() != nullptr ? CookLabel(m_asset->cook) : StringView(u8"-"));
            m_cookButton->OnClick.Add(
                [self](ui::ButtonBase*)
                {
                    if (self->m_asset.Get() == nullptr)
                    {
                        return;
                    }
                    self->m_asset->cook =
                        self->m_asset->cook == draconic::pipeline::CollisionCookKind::ConvexHull
                            ? draconic::pipeline::CollisionCookKind::TriangleMesh
                            : draconic::pipeline::CollisionCookKind::ConvexHull;
                    self->m_cookButton->SetText(CookLabel(self->m_asset->cook));
                    self->MarkDirty();
                });
            row->AddView(m_cookButton.Get());
        }

        // Cook now + status.
        {
            auto row = MakeRef<ui::FlexLayout>(DefaultAllocator());
            row->Direction = ui::Orientation::Horizontal;
            row->Spacing = 8.0f;
            auto cook = MakeRef<ui::Button>(DefaultAllocator(), StringView(u8"Cook now"));
            cook->OnClick.Add([self](ui::ButtonBase*) { (void)self->Save(); });
            row->AddView(cook.Get());
            m_status = MakeRef<ui::Label>(DefaultAllocator(), StringView(u8""));
            m_status->FontSize.SetValue(12.0f);
            auto slp = MakeRef<ui::FlexLayoutParams>(DefaultAllocator());
            slp->Grow = 1.0f;
            slp->AlignSelf = ui::Align::Center;
            row->AddView(m_status.Get(), slp);
            auto lp = MakeRef<ui::FlexLayoutParams>(DefaultAllocator());
            lp->Width = ui::SizeSpec::Match();
            column->AddView(row.Get(), lp);
        }

        m_content = column;
        RefreshStatus();
    }

    void CollisionShapeEditorPage::PickMesh()
    {
        if (m_asset.Get() == nullptr || m_content->Context == nullptr ||
            m_context->Project() == nullptr)
        {
            return;
        }
        Array<String> typeNames;
        typeNames.PushBack(String(u8"StaticMeshAsset"));
        typeNames.PushBack(String(u8"SkinnedMeshAsset"));
        auto dialog =
            MakeRef<app::AssetPickerDialog>(DefaultAllocator(), *m_context, Move(typeNames));
        CollisionShapeEditorPage* self = this;
        dialog->OnPicked = [self](const Guid& picked)
        {
            self->m_asset->sourceMesh = picked;
            self->MarkDirty();
            self->RefreshStatus();
        };
        dialog->Show(m_content->Context);
    }

    void CollisionShapeEditorPage::RefreshStatus()
    {
        if (m_asset.Get() == nullptr)
        {
            return;
        }
        m_meshLabel->SetText(MeshName(m_asset->sourceMesh).AsView());
        String status;
        if (m_asset->sourceMesh.IsNil())
        {
            status = String(u8"No source mesh - pick one, then Cook now.");
        }
        else
        {
            status = String(u8"Cooks a collider from ");
            status.Append(MeshName(m_asset->sourceMesh).AsView());
            status.Append(u8".");
        }
        m_status->SetText(status.AsView());
    }

    Status CollisionShapeEditorPage::Save()
    {
        if (m_asset.Get() == nullptr || m_context->Project() == nullptr)
        {
            return Status{ErrorCode::NotFound};
        }
        content::Instance* instance = m_context->Project()->SourceDb().GetInstance(InstanceId());
        if (instance == nullptr)
        {
            return Status{ErrorCode::NotFound};
        }
        const Status saved = instance->WriteObject(*m_asset);
        if (saved.IsOk())
        {
            ClearDirty();
            m_context->RequestCook(false); // re-cook so a shape=Cooked body gets the collider
            RefreshStatus();
            DRACONIC_LOG_INFO(u8"Editor", u8"saved collision shape '{}'", m_title);
        }
        return saved;
    }
}
