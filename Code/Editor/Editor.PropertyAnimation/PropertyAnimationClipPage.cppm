// Editor::PropertyAnimation - the `editor.propertyanimation` module.
//
// PropertyAnimationClipEditorPage: the standalone authoring PAGE for a PropertyAnimationClipAsset. It
// is now a thin HOST around the shared ClipEditorView (property-animation.md Phase H2): the page owns
// the document (the runtime clip flattened from the cooked source, the authored fileName, save +
// re-cook, the page action toolbar) and implements IClipEditorHost; the view owns all the track /
// curve / transport widgets and drives every edit through the page's command stack. The SAME view is
// docked in the in-scene tool panel (Phase H3), so authoring behaves identically in both places.

module;
#include "Core/Prelude.h"
#include "Core/Log/Log.h"

export module editor.propertyanimation;

import foundation.core;
import foundation.content;
import foundation.vfs;
import foundation.runtime;
import foundation.runtime.client;
import foundation.propertyanimation;
import foundation.propertyanimation.resource;
import propertyanimation.pipeline;
import foundation.ui;
import foundation.ui.toolkit;
import editor.core;
import editor.app;

export import :clip_editor_view;

using namespace foundation::core;

export namespace editor
{
    namespace runtime = foundation::runtime;
    namespace ui = foundation::ui;
    namespace propanim = foundation::propertyanimation;

    // Authoring page for a PropertyAnimationClipAsset. Hosts the shared ClipEditorView.
    class PropertyAnimationClipEditorPage final : public app::UIEditorPage, public IClipEditorHost
    {
    public:
        PropertyAnimationClipEditorPage(EditorContext& context, runtime::IApplicationHost& host,
                                        foundation::content::Instance& instance);

        [[nodiscard]] StringView Title() const override { return m_title.AsView(); }
        [[nodiscard]] ui::View* ContentView() override { return m_content.Get(); }
        [[nodiscard]] Status Save() override;

        // === IClipEditorHost ===
        [[nodiscard]] propanim::PropertyAnimationClip& Clip() override { return m_clip; }
        [[nodiscard]] EditorCommandStack& Commands() override { return EditorPage::Commands(); }
        void MarkClipDirty() override { MarkDirty(); }
        void OnClipViewRebuilt() override
        {
            if (m_toolbar.Get() != nullptr)
            {
                m_toolbar->Refresh();
            }
        }

    private:
        EditorContext* m_context = nullptr;
        String m_title;
        foundation::vfs::SourcePath m_fileName; // the asset's authored fileName (preserved on save)
        propanim::PropertyAnimationClip m_clip; // the runtime editing model (asset source flattens here)

        RefPtr<app::PageToolbar> m_toolbar;
        RefPtr<ui::View> m_content;
        UniquePtr<ClipEditorView> m_view; // the shared editing surface (page is its host)
    };

    class PropertyAnimationClipPageFactory final : public IEditorPageFactory
    {
    public:
        explicit PropertyAnimationClipPageFactory(runtime::IApplicationHost& host) : m_host(&host) {}
        [[nodiscard]] const TypeInfo* PrimaryType() const override
        {
            return &pipeline::PropertyAnimationClipAsset::StaticType();
        }
        [[nodiscard]] UniquePtr<EditorPage>
        CreatePage(EditorContext& context, foundation::content::Instance& instance) override
        {
            return UniquePtr<EditorPage>(
                DefaultAllocator().New<PropertyAnimationClipEditorPage>(context, *m_host, instance),
                DefaultAllocator());
        }

    private:
        runtime::IApplicationHost* m_host;
    };

    // New Asset creator: an empty clip in the invoked group (clips are authored in-editor).
    [[nodiscard]] inline foundation::content::Instance*
    CreatePropertyAnimationClip(EditorContext& context, foundation::content::Group* group)
    {
        foundation::content::Group* target = group;
        if (target == nullptr)
        {
            if (context.Project() == nullptr)
            {
                return nullptr;
            }
            target = context.Project()->SourceDb().RootGroup();
        }
        if (target == nullptr)
        {
            return nullptr;
        }
        const String name = target->UniqueInstanceName(u8"Clip");
        foundation::content::Instance* inst =
            target->CreateInstance(name.AsView(), pipeline::PropertyAnimationClipAsset::StaticType());
        if (inst == nullptr)
        {
            return nullptr;
        }
        pipeline::PropertyAnimationClipAsset asset;
        (void)inst->WriteObject(asset);
        return inst;
    }

    /// The editor executable's entry point for the property-animation plugin.
    inline void RegisterPropertyAnimationEditor(EditorContext& context,
                                                runtime::IApplicationHost& host)
    {
        pipeline::RegisterPropertyAnimationAssets(); // ensure the asset/source/resource types exist
        context.Pages().Register(UniquePtr<IEditorPageFactory>(
            DefaultAllocator().New<PropertyAnimationClipPageFactory>(host), DefaultAllocator()));
        EditorContext::AssetCreator creator;
        creator.label = String(u8"Property Animation Clip");
        creator.category = String(u8"Animation");
        creator.create = &CreatePropertyAnimationClip;
        context.RegisterCreator(Move(creator));
    }
}
