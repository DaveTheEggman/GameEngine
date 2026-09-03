// SPDX-License-Identifier: MIT
// Copyright (c) 2026-Present Robert Campbell

// CollisionShapeEditorPage implementation (see CollisionShapePage.cppm).

module;
#include "Core/Prelude.h"
#include "Core/Log/Log.h"

module editor.physics;

import foundation.core;
import foundation.content;
import foundation.runtime;
import foundation.runtime.client;
import foundation.resource;
import physics.pipeline;
import foundation.physics.resource;
import foundation.render; // debug::DebugDraw + Color (outline wireframe)
import foundation.graphics;
import foundation.ui;
import foundation.ui.runtime;
import foundation.ui.toolkit; // SplitView
import editor.core;
import editor.app;
import editor.preview; // PreviewViewport

using namespace foundation::core;
namespace content = foundation::content;

namespace editor
{
    namespace ui = foundation::ui;
    namespace runtime = foundation::runtime;
    namespace resource = foundation::resource;
    namespace physics = foundation::physics;
    namespace render = foundation::render;

    StringView CollisionShapeEditorPage::CookLabel(pipeline::CollisionCookKind kind)
    {
        return kind == pipeline::CollisionCookKind::ConvexHull ? StringView(u8"Convex hull (dynamic)")
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
        auto row = MakeRef<ui::FlexLayout>(editor::EditorRootAllocator());
        row->Direction = ui::Orientation::Horizontal;
        row->Spacing = 6.0f;
        auto label = MakeRef<ui::Label>(editor::EditorRootAllocator(), labelText);
        auto llp = MakeRef<ui::FlexLayoutParams>(editor::EditorRootAllocator());
        llp->Width = ui::SizeSpec::Fixed(ui::Unit::Dp(90.0f));
        llp->AlignSelf = ui::Align::Center;
        row->AddView(label.Get(), llp);
        ui::FlexLayout* raw = row.Get();
        auto lp = MakeRef<ui::FlexLayoutParams>(editor::EditorRootAllocator());
        lp->Width = ui::SizeSpec::Match();
        column.AddView(row.Get(), lp);
        return raw;
    }

    CollisionShapeEditorPage::CollisionShapeEditorPage(EditorContext& context,
                                                       runtime::IApplicationHost& host,
                                                       ui::runtime::UIHost& uiHost,
                                                       content::Instance& instance)
        : app::UIEditorPage(context.Allocator()),
          m_context(&context), m_host(&host), m_uiHost(&uiHost), m_title(instance.Name())
    {
        SetInstanceId(instance.Id());
        RefPtr<ISerializable> object = instance.ReadObject();
        m_asset =
            RefPtr<pipeline::CollisionShapeAsset>(Cast<pipeline::CollisionShapeAsset>(object.Get()));
        if (m_asset.Get() == nullptr)
        {
            LOG_ERROR(u8"Editor", u8"collision shape '{}' failed to read - page opens empty",
                               m_title);
        }

        auto column = MakeRef<ui::FlexLayout>(Allocator());
        column->Direction = ui::Orientation::Vertical;
        column->Spacing = 8.0f;
        column->Padding = ui::Thickness{10, 8};

        CollisionShapeEditorPage* self = this;

        // Source mesh: a typed picker (no guid string).
        {
            ui::FlexLayout* row = AddLabeledRow(*column, u8"Source mesh");
            m_meshLabel = MakeRef<ui::Label>(Allocator(), StringView(u8""));
            auto lp = MakeRef<ui::FlexLayoutParams>(Allocator());
            lp->Grow = 1.0f;
            lp->AlignSelf = ui::Align::Center;
            row->AddView(m_meshLabel.Get(), lp);
            auto pick = MakeRef<ui::Button>(Allocator(), StringView(u8"Pick..."));
            pick->OnClick.Add([self](ui::ButtonBase*) { self->PickMesh(); });
            row->AddView(pick.Get());
        }

        // Cook kind: a toggle (convex hull / triangle mesh).
        {
            ui::FlexLayout* row = AddLabeledRow(*column, u8"Cook");
            m_cookButton = MakeRef<ui::Button>(
                Allocator(),
                m_asset.Get() != nullptr ? CookLabel(m_asset->cook) : StringView(u8"-"));
            m_cookButton->OnClick.Add(
                [self](ui::ButtonBase*)
                {
                    if (self->m_asset.Get() == nullptr)
                    {
                        return;
                    }
                    self->m_asset->cook =
                        self->m_asset->cook == pipeline::CollisionCookKind::ConvexHull
                            ? pipeline::CollisionCookKind::TriangleMesh
                            : pipeline::CollisionCookKind::ConvexHull;
                    self->m_cookButton->SetText(CookLabel(self->m_asset->cook));
                    self->MarkDirty();
                });
            row->AddView(m_cookButton.Get());
        }

        // Cook now + status.
        {
            auto row = MakeRef<ui::FlexLayout>(Allocator());
            row->Direction = ui::Orientation::Horizontal;
            row->Spacing = 8.0f;
            auto cook = MakeRef<ui::Button>(Allocator(), StringView(u8"Cook now"));
            cook->OnClick.Add([self](ui::ButtonBase*) { (void)self->Save(); });
            row->AddView(cook.Get());
            m_status = MakeRef<ui::Label>(Allocator(), StringView(u8""));
            m_status->FontSize.SetValue(12.0f);
            auto slp = MakeRef<ui::FlexLayoutParams>(Allocator());
            slp->Grow = 1.0f;
            slp->AlignSelf = ui::Align::Center;
            row->AddView(m_status.Get(), slp);
            auto lp = MakeRef<ui::FlexLayoutParams>(Allocator());
            lp->Width = ui::SizeSpec::Match();
            column->AddView(row.Get(), lp);
        }

        // 3D preview substrate (shared): a viewport over a private preview scene. The outline is
        // drawn as unlit debug lines each frame, so the scene needs no entities or light.
        m_preview =
            MakeUnique<PreviewViewport>(Allocator(), *m_host, *m_uiHost, u8"collision.preview");

        // Layout: the outline viewport on the left, the authoring controls on the right.
        auto split = MakeRef<ui::toolkit::SplitView>(Allocator());
        split->SetSplitRatio(0.62f);
        split->SetPanes(m_preview->View(), column.Get());

        // The page action bar (Save / Undo / Redo / Discard) above the split.
        m_toolbar = MakeRef<app::PageToolbar>(Allocator(), *this);
        auto pageColumn = MakeRef<ui::FlexLayout>(Allocator());
        pageColumn->Direction = ui::Orientation::Vertical;
        {
            auto lp = MakeRef<ui::FlexLayoutParams>(Allocator());
            lp->Width = ui::SizeSpec::Match();
            pageColumn->AddView(m_toolbar.Get(), lp);
            auto grow = MakeRef<ui::FlexLayoutParams>(Allocator());
            grow->Grow = 1.0f;
            grow->Width = ui::SizeSpec::Match();
            pageColumn->AddView(split.Get(), grow);
        }
        m_content = pageColumn;

        // Bind the cooked product (auto-follows re-cooks); DrawOutline reads it each frame.
        if (m_context->Resources() != nullptr)
        {
            m_shape = m_context->Resources()->Bind<physics::CollisionShape>(InstanceId());
        }
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
            MakeRef<app::AssetPickerDialog>(Allocator(), *m_context, Move(typeNames));
        CollisionShapeEditorPage* self = this;
        dialog->OnPicked = [self](const Guid& picked)
        {
            self->m_asset->sourceMesh = picked;
            self->MarkDirty();
            self->RefreshStatus();
        };
        dialog->Show(m_content->Context);
    }

    void CollisionShapeEditorPage::DiscardChanges()
    {
        // The page mutates the asset directly (no commands yet), so the base "undo the stack"
        // default would clear dirty WITHOUT reverting - reload from the source DB instead.
        content::Instance* instance = (m_context->Project() != nullptr)
                                          ? m_context->Project()->SourceDb().GetInstance(InstanceId())
                                          : nullptr;
        if (instance != nullptr)
        {
            RefPtr<ISerializable> object = instance->ReadObject();
            if (auto* asset = Cast<pipeline::CollisionShapeAsset>(object.Get()))
            {
                m_asset = RefPtr<pipeline::CollisionShapeAsset>(asset);
                if (m_cookButton.Get() != nullptr)
                {
                    m_cookButton->SetText(CookLabel(m_asset->cook));
                }
                RefreshStatus();
            }
        }
        Commands().Clear();
        ClearDirty();
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
            LOG_INFO(u8"Editor", u8"saved collision shape '{}'", m_title);
        }
        return saved;
    }

    void CollisionShapeEditorPage::DrawOutline()
    {
        if (m_preview.Get() == nullptr || !m_preview->IsValid())
        {
            return;
        }
        physics::CollisionShape* shape = m_shape ? m_shape.Get() : nullptr;
        if (shape == nullptr || shape->outline.Size() < 3)
        {
            return;
        }
        const Array<Float3>& tris = shape->outline; // triangle list: 3 vertices each

        // Frame the camera when the outline first appears or a re-cook changes its vertex count
        // (bounds = min/max over the verts).
        if (tris.Size() != m_framedCount)
        {
            Float3 lo = tris[0];
            Float3 hi = tris[0];
            for (const Float3& v : tris)
            {
                lo = Float3{Min(lo.x, v.x), Min(lo.y, v.y), Min(lo.z, v.z)};
                hi = Float3{Max(hi.x, v.x), Max(hi.y, v.y), Max(hi.z, v.z)};
            }
            const Float3 center = (lo + hi) * 0.5f;
            const f32 radius = Length((hi - lo) * 0.5f);
            m_preview->Camera().FrameBounds(center, radius);
            m_framedCount = tris.Size();
        }

        // Immediate-mode wireframe: three edges per triangle (DebugScene clears each frame).
        render::debug::DebugDraw& draw = m_preview->SceneDebugDraw();
        const Color color{0.30f, 0.95f, 0.55f, 1.0f};
        for (usize i = 0; i + 2 < tris.Size(); i += 3)
        {
            draw.DrawLine(tris[i], tris[i + 1], color);
            draw.DrawLine(tris[i + 1], tris[i + 2], color);
            draw.DrawLine(tris[i + 2], tris[i], color);
        }
    }

    void CollisionShapeEditorPage::OnUpdate(runtime::IApplicationHost&, f32 dt)
    {
        if (m_toolbar.Get() != nullptr)
        {
            m_toolbar->Refresh(); // sync Save/Discard/Undo/Redo enabled state each frame
        }
        if (m_preview.Get() != nullptr)
        {
            m_preview->Update(dt);
        }
        DrawOutline();
    }

    void CollisionShapeEditorPage::OnRenderWindow(runtime::IApplicationHost&,
                                                  foundation::graphics::FrameContext& frame)
    {
        if (m_preview.Get() != nullptr)
        {
            m_preview->RenderFrame(frame);
        }
    }

    void CollisionShapeEditorPage::OnClose()
    {
        if (m_preview.Get() != nullptr)
        {
            m_preview->Shutdown();
        }
    }
}
