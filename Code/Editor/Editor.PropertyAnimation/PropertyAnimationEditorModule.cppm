// Editor::PropertyAnimation - the `editor.propertyanimation` primary module unit.
//
// Property animation is authored IN THE SCENE now (property-animation.md editor redesign): the
// persistent PropertyAnimationPanel (`:panel`) docked below the viewport, over the shared
// ClipEditorView (`:clip_editor_view`). There is no standalone clip PAGE and no viewport tool mode
// anymore (both retired in P1b). This unit is just the plugin registrar: it ensures the clip asset /
// source / resource types exist and contributes the "New Asset -> Property Animation Clip" creator.
// The scene page constructs and owns the panel directly.

module;
#include "Core/Prelude.h"

export module editor.propertyanimation;

import foundation.core;
import foundation.content;
import foundation.runtime;
import foundation.runtime.client; // IApplicationHost (the registrar signature)
import foundation.propertyanimation;
import foundation.propertyanimation.resource;
import propertyanimation.pipeline;
import editor.core;

export import :clip_editor_view;
export import :panel;

using namespace foundation::core;

export namespace editor
{
    namespace runtime = foundation::runtime;

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

    /// The editor executable's entry point for the property-animation plugin. `host` is currently
    /// unused (the panel is scene-page-owned, not a globally registered page), kept for a uniform
    /// registrar signature across the editor plugins.
    inline void RegisterPropertyAnimationEditor(EditorContext& context, runtime::IApplicationHost& host)
    {
        (void)host;
        pipeline::RegisterPropertyAnimationAssets(); // ensure the asset/source/resource types exist
        EditorContext::AssetCreator creator;
        creator.label = String(u8"Property Animation Clip");
        creator.category = String(u8"Animation");
        creator.create = &CreatePropertyAnimationClip;
        context.RegisterCreator(Move(creator));
    }
}
