// Editor::PropertyAnimation - PropertyAnimationTool + providers + the docked panel (heavy bodies out
// of the interface, per the module-hygiene rule).

module;
#include "Core/Prelude.h"
#include "Core/Log/Log.h"

module editor.propertyanimation;

import foundation.core;
import foundation.content;
import foundation.scene;
import foundation.render;
import foundation.propertyanimation;
import foundation.propertyanimation.resource;
import propertyanimation.pipeline;
import foundation.ui;
import editor.core;
import editor.app;
import editor.viewporttools;

using namespace foundation::core;

namespace editor
{
    namespace
    {
        // A component type is usable only if it is reflected: TypeOf<T>() for an unreflected type
        // yields a null name or the "<value>" fallback (the inspector's IsRegisteredType rule).
        bool IsReflectedType(const TypeInfo* type)
        {
            return type != nullptr && type->name != nullptr && type->name[0] != '<';
        }

        void CollectInto(const TypeInfo& componentType, const TypeInfo& current, const String& prefix,
                         Array<AnimatablePropertyInfo>& out)
        {
            for (const PropertyInfo& prop : Properties(current))
            {
                if (prop.type == nullptr || IsContainer(*prop.type))
                {
                    continue;
                }
                String path = prefix;
                if (!path.IsEmpty())
                {
                    path += u8".";
                }
                path += StringView(reinterpret_cast<const utf8char*>(prop.name));
                if (IsNested(prop))
                {
                    CollectInto(componentType, *prop.type, path, out); // recurse nested structs
                    continue;
                }
                const Optional<propanim::TrackValueKind> kind = InferTrackKind(prop.type);
                if (!kind.HasValue())
                {
                    continue;
                }
                // Only seed properties the runtime can actually resolve + drive.
                if (!propanim::ResolveBinding(componentType, path.AsView()).IsResolved())
                {
                    continue;
                }
                AnimatablePropertyInfo info;
                info.componentType =
                    String(StringView(reinterpret_cast<const utf8char*>(componentType.name)));
                info.propertyPath = Move(path);
                info.kind = kind.Value();
                out.PushBack(Move(info));
            }
        }
    }

    Optional<propanim::TrackValueKind> InferTrackKind(const TypeInfo* leafType)
    {
        if (leafType == &TypeOf<f32>())
        {
            return propanim::TrackValueKind::Float;
        }
        if (leafType == &TypeOf<Float3>())
        {
            return propanim::TrackValueKind::Float3;
        }
        if (leafType == &TypeOf<Color>())
        {
            return propanim::TrackValueKind::Color;
        }
        if (leafType == &TypeOf<Quaternion>())
        {
            return propanim::TrackValueKind::Quat;
        }
        return {};
    }

    void CollectAnimatableProperties(const TypeInfo& componentType, Array<AnimatablePropertyInfo>& out)
    {
        CollectInto(componentType, componentType, String{}, out);
    }

    // === PropertyAnimationTool ===

    PropertyAnimationTool::PropertyAnimationTool(const ViewportToolHostContext& ctx,
                                                 EditorContext& editorCtx)
        : m_editorCtx(&editorCtx), m_scene(ctx.scene), m_commands(ctx.commands),
          m_selection(ctx.entitySelection)
    {
        m_status = String(u8"Property Animation: no clip - New or Pick a clip");
    }

    void PropertyAnimationTool::ClearClip()
    {
        m_clip = propanim::PropertyAnimationClip{};
        m_clipId = Guid{};
        m_clipName = String{};
        m_dirty = false;
    }

    void PropertyAnimationTool::NewClip()
    {
        foundation::content::Instance* inst = CreatePropertyAnimationClip(*m_editorCtx, nullptr);
        if (inst == nullptr)
        {
            return;
        }
        LoadClip(inst->Id());
    }

    void PropertyAnimationTool::LoadClip(const Guid& instanceId)
    {
        if (m_editorCtx->Project() == nullptr)
        {
            return;
        }
        foundation::content::Instance* inst =
            m_editorCtx->Project()->SourceDb().GetInstance(instanceId);
        if (inst == nullptr)
        {
            return;
        }
        ClearClip();
        RefPtr<ISerializable> object = inst->ReadObject();
        if (auto* asset = Cast<pipeline::PropertyAnimationClipAsset>(object.Get()))
        {
            asset->source.FillClip(m_clip);
        }
        m_clipId = instanceId;
        m_clipName = String(inst->Name());
        m_status = String(u8"Property Animation: editing '");
        m_status += m_clipName.AsView();
        m_status += u8"'";
    }

    void PropertyAnimationTool::SaveClip()
    {
        if (m_clipId.IsNil() || m_editorCtx->Project() == nullptr)
        {
            return;
        }
        foundation::content::Instance* inst = m_editorCtx->Project()->SourceDb().GetInstance(m_clipId);
        if (inst == nullptr)
        {
            return;
        }
        pipeline::PropertyAnimationClipAsset asset;
        propanim::PropertyAnimationClipSource::FromClip(m_clip, asset.source);
        if (inst->WriteObject(asset).IsOk())
        {
            m_dirty = false;
            m_editorCtx->RequestCook(false);
            LOG_INFO(u8"Editor", u8"saved in-scene property-animation clip '{}'", m_clipName);
        }
    }

    usize PropertyAnimationTool::AddTracksFromSelection(ClipEditorView& view)
    {
        if (m_scene == nullptr || m_selection == nullptr)
        {
            return 0;
        }
        const Guid* primary = m_selection->Primary();
        if (primary == nullptr)
        {
            return 0;
        }
        const scene::EntityHandle entity = m_scene->FindEntity(*primary);
        if (!entity.IsAssigned())
        {
            return 0;
        }
        Array<AnimatablePropertyInfo> seeds;
        m_scene->ForEachManager(
            [&](scene::ComponentManagerBase& mgr)
            {
                if (!mgr.HasComponent(entity))
                {
                    return;
                }
                const TypeInfo* type = mgr.ComponentType();
                if (!IsReflectedType(type))
                {
                    return;
                }
                CollectAnimatableProperties(*type, seeds);
            });
        if (seeds.IsEmpty())
        {
            return 0;
        }
        // One undo step for the whole seed set (the stack coalesces Executes between the brackets).
        m_commands->BeginGroup(u8"propanim-add-tracks-from-selection");
        for (const AnimatablePropertyInfo& seed : seeds)
        {
            view.AddTrack(seed.componentType.AsView(), seed.propertyPath.AsView(), seed.kind);
        }
        m_commands->EndGroup();
        return seeds.Size();
    }

    // === live preview (Phase H4) ===
    //
    // Scrubbing writes the clip's sampled values onto the SELECTED entity through the exact runtime
    // path the component manager uses (FindManager -> ResolveBinding -> GetComponentInstance ->
    // WriteBinding), so the generation guard and per-write binding re-resolve come for free. Writes
    // are TRANSIENT: a snapshot is captured on the first scrub, restored on stop / deactivate / when
    // Simulate begins. Nothing goes through the command stack or MarkDirty, so the document stays
    // clean and the preview is never undoable (Fable Q3).

    scene::ComponentManagerBase* PropertyAnimationTool::FindManagerByComponentTypeName(StringView name)
    {
        if (m_scene == nullptr)
        {
            return nullptr;
        }
        scene::ComponentManagerBase* found = nullptr;
        m_scene->ForEachManager(
            [&](scene::ComponentManagerBase& m)
            {
                if (found == nullptr && m.ComponentType() != nullptr && m.ComponentType()->name &&
                    StringView(reinterpret_cast<const utf8char*>(m.ComponentType()->name)) == name)
                {
                    found = &m;
                }
            });
        return found;
    }

    bool PropertyAnimationTool::Update(const ViewportToolInput& input)
    {
        m_editingLocked = input.editingLocked;
        if (m_editingLocked)
        {
            StopPreview(); // no preview outside EDIT (Simulate/Play): restore + stand down
        }
        return false; // the tool consumes no viewport gesture (yet)
    }

    void PropertyAnimationTool::OnDeactivate()
    {
        StopPreview(); // leaving the tool restores the previewed entity
    }

    void PropertyAnimationTool::OnScrubTimeChanged(f32 time)
    {
        if (m_clip.tracks.IsEmpty() || m_editingLocked || m_scene == nullptr || m_selection == nullptr)
        {
            return;
        }
        const Guid* primary = m_selection->Primary();
        if (primary == nullptr)
        {
            StopPreview();
            return;
        }
        const scene::EntityHandle entity = m_scene->FindEntity(*primary);
        if (!entity.IsAssigned())
        {
            StopPreview();
            return;
        }
        PreviewAt(entity, time);
    }

    void PropertyAnimationTool::SnapshotEntity(scene::EntityHandle entity)
    {
        m_snapshot.Clear();
        for (const propanim::PropertyTrack& track : m_clip.tracks)
        {
            scene::ComponentManagerBase* mgr =
                FindManagerByComponentTypeName(track.componentType.AsView());
            if (mgr == nullptr || mgr->ComponentType() == nullptr || !mgr->HasComponent(entity))
            {
                continue;
            }
            const propanim::PropertyBinding binding =
                propanim::ResolveBinding(*mgr->ComponentType(), track.propertyPath.AsView());
            if (!binding.IsResolved())
            {
                continue;
            }
            Variant current = propanim::ReadBinding(binding, mgr->GetComponentInstance(entity));
            if (current.IsEmpty())
            {
                continue;
            }
            PreviewSnapshotEntry entry;
            entry.componentType = track.componentType;
            entry.propertyPath = track.propertyPath;
            entry.value = Move(current);
            m_snapshot.PushBack(Move(entry));
        }
    }

    void PropertyAnimationTool::PreviewAt(scene::EntityHandle entity, f32 time)
    {
        if (!m_previewing || m_previewEntity != entity)
        {
            if (m_previewing)
            {
                StopPreview(); // selection moved to a different entity: restore the old one first
            }
            SnapshotEntity(entity);
            m_previewing = true;
            m_previewEntity = entity;
        }
        m_previewTime = time;
        for (const propanim::PropertyTrack& track : m_clip.tracks)
        {
            scene::ComponentManagerBase* mgr =
                FindManagerByComponentTypeName(track.componentType.AsView());
            if (mgr == nullptr || mgr->ComponentType() == nullptr || !mgr->HasComponent(entity))
            {
                continue;
            }
            const propanim::PropertyBinding binding =
                propanim::ResolveBinding(*mgr->ComponentType(), track.propertyPath.AsView());
            if (!binding.IsResolved())
            {
                continue;
            }
            // GetComponentInstance re-resolves live every write (structural-change safe).
            (void)propanim::WriteBinding(binding, mgr->GetComponentInstance(entity), track.Sample(time));
        }
    }

    void PropertyAnimationTool::StopPreview()
    {
        if (!m_previewing)
        {
            return;
        }
        if (m_scene != nullptr && m_previewEntity.IsAssigned())
        {
            for (const PreviewSnapshotEntry& entry : m_snapshot)
            {
                scene::ComponentManagerBase* mgr =
                    FindManagerByComponentTypeName(entry.componentType.AsView());
                if (mgr == nullptr || mgr->ComponentType() == nullptr ||
                    !mgr->HasComponent(m_previewEntity))
                {
                    continue;
                }
                const propanim::PropertyBinding binding =
                    propanim::ResolveBinding(*mgr->ComponentType(), entry.propertyPath.AsView());
                if (!binding.IsResolved())
                {
                    continue;
                }
                (void)propanim::WriteBinding(binding, mgr->GetComponentInstance(m_previewEntity),
                                             entry.value);
            }
        }
        m_snapshot.Clear();
        m_previewing = false;
        m_previewEntity = scene::EntityHandle{};
    }

    void PropertyAnimationTool::Draw(foundation::render::debug::DebugDraw& drawList)
    {
        if (!m_previewing || m_scene == nullptr || !m_previewEntity.IsAssigned())
        {
            return;
        }
        // Best-effort marker at the previewed entity's Transform position (overlay so it reads over
        // geometry). Absent/foreign transform component: no marker, but the preview still applies.
        scene::ComponentManagerBase* mgr = FindManagerByComponentTypeName(u8"Transform");
        if (mgr == nullptr || mgr->ComponentType() == nullptr || !mgr->HasComponent(m_previewEntity))
        {
            return;
        }
        const propanim::PropertyBinding binding =
            propanim::ResolveBinding(*mgr->ComponentType(), u8"position");
        if (!binding.IsResolved())
        {
            return;
        }
        const Variant pos = propanim::ReadBinding(binding, mgr->GetComponentInstance(m_previewEntity));
        if (pos.IsEmpty() || !pos.Is<Float3>())
        {
            return;
        }
        const Float3 p = pos.Get<Float3>();
        const Color marker{1.0f, 0.85f, 0.2f, 1.0f};
        drawList.DrawWireSphereOverlay(p, 0.35f, marker);
        drawList.DrawText3D(p, u8"preview", marker);
    }

    // === PropertyAnimationToolProvider ===

    void PropertyAnimationToolProvider::CreateTools(ViewportToolManager& manager,
                                                    const ViewportToolHostContext& context)
    {
        manager.Add(MakeUnique<PropertyAnimationTool>(DefaultAllocator(), context, *m_editorCtx));
    }

    // === PropertyAnimationPanelView (the docked panel: clip chrome + the shared ClipEditorView) ===

    namespace
    {
        class PropertyAnimationPanelView final : public ui::FlexLayout
        {
        public:
            PropertyAnimationPanelView(PropertyAnimationTool& tool, EditorContext& editorCtx)
                : m_tool(&tool), m_editorCtx(&editorCtx)
            {
                Direction = ui::Orientation::Vertical;
                Padding = ui::Thickness{6, 6};
                Spacing = 4.0f;

                auto header = MakeRef<ui::FlexLayout>(DefaultAllocator());
                header->Direction = ui::Orientation::Horizontal;
                header->Spacing = 4.0f;
                PropertyAnimationPanelView* self = this;
                AddButton(*header, u8"New", 52.0f, [self]() { self->OnNew(); });
                AddButton(*header, u8"Pick...", 60.0f, [self]() { self->OnPick(); });
                AddButton(*header, u8"+ From Selection", 128.0f, [self]() { self->OnAddFromSelection(); });
                AddButton(*header, u8"Save", 52.0f, [self]() { self->OnSave(); });
                {
                    auto lp = MakeRef<ui::FlexLayoutParams>(DefaultAllocator());
                    lp->Width = ui::SizeSpec::Match();
                    lp->Height = ui::SizeSpec::Fixed(ui::Unit::Px(26));
                    AddView(header.Get(), lp);
                }

                m_clipLabel = MakeRef<ui::Label>(DefaultAllocator(), StringView(u8""));
                m_clipLabel->FontSize.SetValue(11.0f);
                {
                    auto lp = MakeRef<ui::FlexLayoutParams>(DefaultAllocator());
                    lp->Width = ui::SizeSpec::Match();
                    lp->Height = ui::SizeSpec::Fixed(ui::Unit::Px(18));
                    AddView(m_clipLabel.Get(), lp);
                }

                m_view = MakeUnique<ClipEditorView>(DefaultAllocator(), tool);
                {
                    auto lp = MakeRef<ui::FlexLayoutParams>(DefaultAllocator());
                    lp->Width = ui::SizeSpec::Match();
                    lp->Grow = 1.0f;
                    AddView(m_view->Root(), lp);
                }
                RefreshHeader();
            }

        private:
            void AddButton(ui::FlexLayout& row, StringView label, f32 width, Function<void()> onClick)
            {
                auto button = MakeRef<ui::Button>(DefaultAllocator(), label);
                button->FontSize.SetValue(Optional<f32>{11.0f});
                button->OnClick.Add([fn = Move(onClick)](ui::ButtonBase*)
                                    { if (fn) fn(); });
                auto lp = MakeRef<ui::FlexLayoutParams>(DefaultAllocator());
                lp->Width = ui::SizeSpec::Fixed(ui::Unit::Px(width));
                lp->Height = ui::SizeSpec::Match();
                row.AddView(button.Get(), lp);
            }

            void RefreshHeader()
            {
                String text = m_tool->HasClip() ? String(u8"Clip: ") : String(u8"Clip: (none - New or Pick)");
                if (m_tool->HasClip())
                {
                    text += m_tool->ClipName();
                }
                m_clipLabel->SetText(text.AsView());
            }

            void OnNew()
            {
                m_tool->NewClip();
                m_view->ResetForClip();
                RefreshHeader();
            }

            void OnPick()
            {
                ui::UIContext* ctx = this->Context;
                if (ctx == nullptr || m_editorCtx->Project() == nullptr)
                {
                    return;
                }
                Array<String> types;
                types.PushBack(String(u8"PropertyAnimationClipAsset"));
                auto dialog =
                    MakeRef<app::AssetPickerDialog>(DefaultAllocator(), *m_editorCtx, Move(types));
                PropertyAnimationPanelView* self = this;
                dialog->OnPicked = [self](const Guid& picked)
                {
                    if (!picked.IsNil())
                    {
                        self->m_tool->LoadClip(picked);
                        self->m_view->ResetForClip();
                        self->RefreshHeader();
                    }
                };
                dialog->Show(ctx);
            }

            void OnAddFromSelection()
            {
                const usize added = m_tool->AddTracksFromSelection(*m_view);
                if (added == 0)
                {
                    LOG_INFO(u8"Editor",
                             u8"add-from-selection: no selected entity or no animatable properties");
                }
            }

            void OnSave()
            {
                m_tool->SaveClip();
                RefreshHeader();
            }

            PropertyAnimationTool* m_tool;
            EditorContext* m_editorCtx;
            UniquePtr<ClipEditorView> m_view;
            RefPtr<ui::Label> m_clipLabel;
        };
    }

    // === PropertyAnimationPanelProvider ===

    RefPtr<foundation::ui::View>
    PropertyAnimationPanelProvider::CreatePanel(IViewportTool& tool, const ViewportToolHostContext&)
    {
        if (tool.Id() != ToolId())
        {
            return {}; // defensive: the registry keys on id, but never downcast a mismatched tool
        }
        auto& pat = static_cast<PropertyAnimationTool&>(tool);
        return MakeRef<PropertyAnimationPanelView>(DefaultAllocator(), pat, *m_editorCtx);
    }
}
