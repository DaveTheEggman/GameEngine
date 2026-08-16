// Editor::PropertyAnimation - the clip page host implementation (document load + chrome + Save). The
// editing widgets live in ClipEditorView (this page is just its IClipEditorHost).

module;
#include "Core/Prelude.h"
#include "Core/Log/Log.h"

module editor.propertyanimation;

import foundation.core;
import foundation.content;
import foundation.propertyanimation;
import foundation.propertyanimation.resource;
import propertyanimation.pipeline;
import foundation.ui;
import editor.core;
import editor.app;

using namespace foundation::core;

namespace editor
{
    PropertyAnimationClipEditorPage::PropertyAnimationClipEditorPage(
        EditorContext& context, runtime::IApplicationHost&, foundation::content::Instance& instance)
        : m_context(&context), m_title(instance.Name())
    {
        RefPtr<ISerializable> object = instance.ReadObject();
        if (auto* asset = Cast<pipeline::PropertyAnimationClipAsset>(object.Get()))
        {
            m_fileName = asset->fileName;   // preserved verbatim on save
            asset->source.FillClip(m_clip); // flatten the cooked wire into the editing model
        }

        auto column = MakeRef<ui::FlexLayout>(DefaultAllocator());
        column->Direction = ui::Orientation::Vertical;
        column->Padding = ui::Thickness{8, 6};

        // Page chrome (Save / Undo / Redo / Discard) stays with the host; the shared view has none.
        m_toolbar = MakeRef<app::PageToolbar>(DefaultAllocator(), *this);
        {
            auto lp = MakeRef<ui::FlexLayoutParams>(DefaultAllocator());
            lp->Width = ui::SizeSpec::Match();
            lp->Height = ui::SizeSpec::Fixed(ui::Unit::Px(28));
            column->AddView(m_toolbar.Get(), lp);
        }

        // The shared editing surface (constructed AFTER the toolbar so its first rebuild can refresh
        // the toolbar via OnClipViewRebuilt).
        m_view = MakeUnique<ClipEditorView>(DefaultAllocator(), *this);
        {
            auto lp = MakeRef<ui::FlexLayoutParams>(DefaultAllocator());
            lp->Width = ui::SizeSpec::Match();
            lp->Grow = 1.0f;
            column->AddView(m_view->Root(), lp);
        }
        m_content = column;
    }

    Status PropertyAnimationClipEditorPage::Save()
    {
        if (m_context->Project() == nullptr)
        {
            return Status{ErrorCode::NotFound};
        }
        foundation::content::Instance* instance =
            m_context->Project()->SourceDb().GetInstance(InstanceId());
        if (instance == nullptr)
        {
            return Status{ErrorCode::NotFound};
        }
        // Flatten the edited runtime clip back into a fresh asset (the asset type is non-copyable),
        // preserving the authored fileName, then persist + re-cook.
        pipeline::PropertyAnimationClipAsset asset;
        asset.fileName = m_fileName;
        propanim::PropertyAnimationClipSource::FromClip(m_clip, asset.source);
        const Status saved = instance->WriteObject(asset);
        if (saved.IsOk())
        {
            ClearDirty();
            m_context->RequestCook(false);
            LOG_INFO(u8"Editor", u8"saved property-animation clip '{}'", m_title);
        }
        return saved;
    }
}
