// Editor::PropertyAnimation - the `editor.propertyanimation` module.
//
// PropertyAnimationClipPage (property-animation.md Editor, P1 - NO curve canvas yet): the authoring
// surface for a PropertyAnimationClipAsset. A scrollable track list (each track = component-type +
// property path + value kind, add/remove) with a keyframe TABLE per track (time + value-per-channel
// + interpolation, add/remove), plus a transport row (play/scrub) that samples the clip at the scrub
// time and shows the per-track values. Editing model is the runtime PropertyAnimationClip (easy to
// mutate); Save flattens it back to the asset's cooked source + requests a re-cook. Every edit is one
// UNDOABLE command over a whole-clip snapshot (small data; the InputMapPage idiom). Rebuilds defer
// through the UI mutation queue (the never-free-a-view-mid-dispatch rule).

module;
#include "Core/Prelude.h"
#include "Core/Log/Log.h"
#include <cstdlib>

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

using namespace foundation::core;

export namespace editor
{
    namespace runtime = foundation::runtime;
    namespace ui = foundation::ui;
    namespace propanim = foundation::propertyanimation;

    // Authoring page for a PropertyAnimationClipAsset.
    class PropertyAnimationClipEditorPage final : public app::UIEditorPage
    {
    public:
        PropertyAnimationClipEditorPage(EditorContext& context, runtime::IApplicationHost& host,
                                        foundation::content::Instance& instance);

        [[nodiscard]] StringView Title() const override { return m_title.AsView(); }
        [[nodiscard]] ui::View* ContentView() override { return m_content.Get(); }
        [[nodiscard]] Status Save() override;

    private:
        // Every edit = one undoable command over a whole-clip snapshot (the clip is small data).
        class ClipEditCommand final : public IEditorCommand
        {
        public:
            ClipEditCommand(PropertyAnimationClipEditorPage& page, propanim::PropertyAnimationClip before,
                            propanim::PropertyAnimationClip after)
                : m_page(&page), m_before(Move(before)), m_after(Move(after))
            {
            }
            [[nodiscard]] bool Execute() override
            {
                m_page->m_clip = m_after;
                m_page->RequestRebuild();
                return true;
            }
            void Undo() override
            {
                m_page->m_clip = m_before;
                m_page->RequestRebuild();
            }
            [[nodiscard]] StringView TypeId() const override { return u8"propanim-clip-edit"; }

        private:
            PropertyAnimationClipEditorPage* m_page;
            propanim::PropertyAnimationClip m_before;
            propanim::PropertyAnimationClip m_after;
        };

        // Snapshot -> mutate -> push. `fn` edits a COPY that becomes the new clip.
        template <typename Fn>
        void Mutate(Fn&& fn)
        {
            propanim::PropertyAnimationClip before = m_clip;
            propanim::PropertyAnimationClip after = m_clip;
            fn(after);
            after.duration = after.ComputeDuration();
            (void)Commands().Execute(UniquePtr<IEditorCommand>(
                DefaultAllocator().New<ClipEditCommand>(*this, Move(before), Move(after)),
                DefaultAllocator()));
        }

        // --- UI construction (bodies in the impl unit) ---
        void RequestRebuild(); // defer a Rebuild through the mutation queue (mid-dispatch safe)
        void Rebuild();
        [[nodiscard]] RefPtr<ui::FlexLayout> MakeRow(f32 indent, f32 height = 24.0f);
        ui::Button* MakeButton(ui::FlexLayout& row, StringView label, f32 width,
                               Function<void()> onClick);
        void AddLabel(ui::FlexLayout& row, StringView text, f32 grow = 0.0f, f32 width = 0.0f);
        void AddTextField(ui::FlexLayout& row, StringView value, Function<void(StringView)> commit,
                          f32 width);
        void AddFloatField(ui::FlexLayout& row, f32 value, Function<void(f32)> commit,
                           f32 width = 56.0f);
        void BuildTransportRow();
        void BuildTrackRows(usize trackIndex);
        void RefreshPreview(); // update the sampled-values readout at the current scrub time

        // CurveCanvas host for a scalar track (Float/Float3/Color): push the track's channels into the
        // canvas (times normalized by m_editDuration), and read them back on edit (live + one undo per
        // gesture). Quat tracks keep the numeric key table (no per-component curve).
        void AddCurveCanvas(usize trackIndex);
        void PushTrackToCanvas(usize trackIndex, ui::toolkit::CurveCanvas& canvas);
        void WriteBackTrack(usize trackIndex, ui::toolkit::CurveCanvas& canvas);

        [[nodiscard]] static StringView KindName(propanim::TrackValueKind kind);
        [[nodiscard]] static StringView InterpName(CurveKeyInterpolation interp);
        [[nodiscard]] static ui::toolkit::CurveInterpolation ClipToCanvasInterp(CurveKeyInterpolation i);
        [[nodiscard]] static CurveKeyInterpolation CanvasToClipInterp(ui::toolkit::CurveInterpolation i);

        EditorContext* m_context = nullptr;
        String m_title;
        foundation::vfs::SourcePath m_fileName;  // the asset's authored fileName (preserved on save)
        propanim::PropertyAnimationClip m_clip;  // the runtime editing model (asset source flattens here)

        RefPtr<app::PageToolbar> m_toolbar;
        RefPtr<ui::View> m_content;
        RefPtr<ui::ScrollView> m_scroll;
        RefPtr<ui::FlexLayout> m_rows;
        RefPtr<ui::Label> m_preview; // sampled values at the scrub time
        f32 m_scrubTime = 0.0f;
        f32 m_editDuration = 1.0f;   // the canvas time-axis scale (clip length); keys normalize by it
        propanim::PropertyAnimationClip m_gestureBefore; // undo snapshot captured on OnEditBegin
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
