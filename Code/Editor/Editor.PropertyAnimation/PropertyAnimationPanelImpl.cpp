// Editor::PropertyAnimation - PropertyAnimationPanel (the persistent in-scene editor): clip document
// management, live preview, and the docked chrome (Timeline scrubber + header over the shared
// ClipEditorView). Heavy bodies out of the interface, per the module-hygiene rule.

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
import foundation.ui.toolkit;
import editor.core;
import editor.app;

using namespace foundation::core;
namespace core = foundation::core; // explicit: `Transform` is ambiguous once ui.toolkit is imported

namespace editor
{
    namespace
    {
        // The entity's local transform is animated by naming it with this reserved component type
        // (it is baked into the scene, not a reflected component - see the engine manager).
        constexpr StringView kTransformName = u8"Transform";

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

    // === PropertyAnimationPanel: construction + chrome ===

    PropertyAnimationPanel::PropertyAnimationPanel(EditorContext& editorCtx, scene::Scene& scene,
                                                   EditorCommandStack& commands,
                                                   Selection<Guid>& selection)
        : m_editorCtx(&editorCtx), m_scene(&scene), m_commands(&commands), m_selection(&selection)
    {
        BuildChrome();
        RefreshHeader();
    }

    void PropertyAnimationPanel::BuildChrome()
    {
        Direction = ui::Orientation::Vertical;
        Padding = ui::Thickness{6, 6};
        Spacing = 4.0f;

        PropertyAnimationPanel* self = this;

        auto addButton = [](ui::FlexLayout& row, StringView label, f32 width, Function<void()> onClick)
        {
            auto button = MakeRef<ui::Button>(DefaultAllocator(), label);
            button->FontSize.SetValue(Optional<f32>{11.0f});
            button->OnClick.Add([fn = Move(onClick)](ui::ButtonBase*) { if (fn) fn(); });
            auto lp = MakeRef<ui::FlexLayoutParams>(DefaultAllocator());
            lp->Width = ui::SizeSpec::Fixed(ui::Unit::Px(width));
            lp->Height = ui::SizeSpec::Match();
            row.AddView(button.Get(), lp);
            return button;
        };

        // Header: [collapse caret] New / Pick / + From Selection / Save + clip label.
        auto header = MakeRef<ui::FlexLayout>(DefaultAllocator());
        header->Direction = ui::Orientation::Horizontal;
        header->Spacing = 4.0f;
        m_collapseButton = addButton(*header, u8"v", 26.0f, [self]() { self->SetCollapsed(!self->m_collapsed); });
        addButton(*header, u8"New", 52.0f, [self]() { self->OnNew(); });
        addButton(*header, u8"Pick...", 60.0f, [self]() { self->OnPick(); });
        addButton(*header, u8"+ From Selection", 128.0f, [self]() { self->OnAddFromSelection(); });
        addButton(*header, u8"Save", 52.0f, [self]() { self->OnSave(); });
        m_clipLabel = MakeRef<ui::Label>(DefaultAllocator(), StringView(u8""));
        m_clipLabel->FontSize.SetValue(11.0f);
        {
            auto lp = MakeRef<ui::FlexLayoutParams>(DefaultAllocator());
            lp->Grow = 1.0f;
            lp->Height = ui::SizeSpec::Match();
            header->AddView(m_clipLabel.Get(), lp);
        }
        {
            auto lp = MakeRef<ui::FlexLayoutParams>(DefaultAllocator());
            lp->Width = ui::SizeSpec::Match();
            lp->Height = ui::SizeSpec::Fixed(ui::Unit::Px(26));
            AddView(header.Get(), lp);
        }

        // Body: the Timeline scrubber above the shared ClipEditorView. Hidden when collapsed.
        m_body = MakeRef<ui::FlexLayout>(DefaultAllocator());
        m_body->Direction = ui::Orientation::Vertical;
        m_body->Spacing = 4.0f;

        m_timeline = MakeRef<ui::toolkit::Timeline>(DefaultAllocator());
        m_timeline->SetDuration(Max(m_clip.ComputeDuration(), 1.0f));
        m_timeline->OnPlayheadMoved.Add(
            [self](f32 t)
            {
                if (self->m_view)
                {
                    self->m_view->SetScrubTime(t);
                }
                self->OnScrubTimeChanged(t);
            });
        {
            auto lp = MakeRef<ui::FlexLayoutParams>(DefaultAllocator());
            lp->Width = ui::SizeSpec::Match();
            lp->Height = ui::SizeSpec::Fixed(ui::Unit::Px(28));
            m_body->AddView(m_timeline.Get(), lp);
        }

        m_view = MakeUnique<ClipEditorView>(DefaultAllocator(), *this);
        {
            auto lp = MakeRef<ui::FlexLayoutParams>(DefaultAllocator());
            lp->Width = ui::SizeSpec::Match();
            lp->Grow = 1.0f;
            m_body->AddView(m_view->Root(), lp);
        }
        {
            auto lp = MakeRef<ui::FlexLayoutParams>(DefaultAllocator());
            lp->Width = ui::SizeSpec::Match();
            lp->Grow = 1.0f;
            AddView(m_body.Get(), lp);
        }
    }

    void PropertyAnimationPanel::RefreshHeader()
    {
        String text = HasClip() ? String(u8"Clip: ") : String(u8"Clip: (none - New or Pick)");
        if (HasClip())
        {
            text += ClipName();
        }
        if (m_clipLabel.Get() != nullptr)
        {
            m_clipLabel->SetText(text.AsView());
        }
    }

    void PropertyAnimationPanel::SetCollapsed(bool collapsed)
    {
        if (m_collapsed == collapsed)
        {
            return;
        }
        m_collapsed = collapsed;
        if (m_body.Get() != nullptr)
        {
            m_body->Visibility = collapsed ? ui::Visibility::Gone : ui::Visibility::Visible;
        }
        if (m_collapseButton.Get() != nullptr)
        {
            m_collapseButton->SetText(collapsed ? StringView(u8">") : StringView(u8"v"));
        }
        Invalidate();
    }

    // === header actions ===

    void PropertyAnimationPanel::OnNew()
    {
        NewClip();
        if (m_view)
        {
            m_view->ResetForClip();
        }
        RefreshHeader();
    }

    void PropertyAnimationPanel::OnPick()
    {
        ui::UIContext* ctx = this->Context;
        if (ctx == nullptr || m_editorCtx->Project() == nullptr)
        {
            return;
        }
        Array<String> types;
        types.PushBack(String(u8"PropertyAnimationClipAsset"));
        auto dialog = MakeRef<app::AssetPickerDialog>(DefaultAllocator(), *m_editorCtx, Move(types));
        PropertyAnimationPanel* self = this;
        dialog->OnPicked = [self](const Guid& picked)
        {
            if (!picked.IsNil())
            {
                self->LoadClip(picked);
                if (self->m_view)
                {
                    self->m_view->ResetForClip();
                }
                self->RefreshHeader();
            }
        };
        dialog->Show(ctx);
    }

    void PropertyAnimationPanel::OnAddFromSelection()
    {
        const usize added = AddTracksFromSelection(*m_view);
        if (added == 0)
        {
            LOG_INFO(u8"Editor",
                     u8"add-from-selection: no selected entity or no animatable properties");
        }
    }

    void PropertyAnimationPanel::OnSave()
    {
        SaveClip();
        RefreshHeader();
    }

    // === clip document management ===

    void PropertyAnimationPanel::ClearClip()
    {
        StopPreview(); // never leave a live preview pointing at the old clip's tracks
        m_clip = propanim::PropertyAnimationClip{};
        m_clipId = Guid{};
        m_clipName = String{};
        m_dirty = false;
    }

    void PropertyAnimationPanel::NewClip()
    {
        foundation::content::Instance* inst = CreatePropertyAnimationClip(*m_editorCtx, nullptr);
        if (inst == nullptr)
        {
            return;
        }
        LoadClip(inst->Id());
    }

    void PropertyAnimationPanel::LoadClip(const Guid& instanceId)
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
    }

    void PropertyAnimationPanel::SaveClip()
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

    usize PropertyAnimationPanel::AddTracksFromSelection(ClipEditorView& view)
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
        // Every entity has a scene transform (not a reflected component), so always offer its TRS.
        seeds.PushBack(AnimatablePropertyInfo{String(kTransformName), String(u8"position"),
                                              propanim::TrackValueKind::Float3});
        seeds.PushBack(AnimatablePropertyInfo{String(kTransformName), String(u8"rotation"),
                                              propanim::TrackValueKind::Quat});
        seeds.PushBack(AnimatablePropertyInfo{String(kTransformName), String(u8"scale"),
                                              propanim::TrackValueKind::Float3});
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
        m_commands->LockGroup(); // a second "+ From Selection" is its OWN undo entry, not merged
        return seeds.Size();
    }

    // === live preview ===
    //
    // Scrubbing writes the clip's sampled values onto the SELECTED entity through the exact runtime
    // path the component manager uses (FindManager -> ResolveBinding -> GetComponentInstance ->
    // WriteBinding), so the generation guard and per-write binding re-resolve come for free. Writes
    // are TRANSIENT: a snapshot is captured on the first scrub, restored on stop / Simulate / clip
    // change. Nothing goes through the command stack or MarkDirty, so the document stays clean and the
    // preview is never undoable. The snapshot is re-taken when the TRACK SET changes (the #6 fix) so it
    // never restores stale targets after a track is added / removed / retargeted.

    scene::ComponentManagerBase*
    PropertyAnimationPanel::FindManagerByComponentTypeName(StringView name)
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

    void PropertyAnimationPanel::Tick(bool editingLocked)
    {
        m_editingLocked = editingLocked;
        if (m_editingLocked)
        {
            StopPreview(); // no preview outside EDIT (Simulate/Play): restore + stand down
        }
    }

    void PropertyAnimationPanel::OnScrubTimeChanged(f32 time)
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

    void PropertyAnimationPanel::OnClipViewRebuilt()
    {
        // The clip length may have changed (a Length edit / new document): resync the Timeline axis.
        if (m_timeline.Get() != nullptr)
        {
            m_timeline->SetDuration(Max(m_clip.ComputeDuration(), 1.0f));
        }
    }

    Variant PropertyAnimationPanel::ReadTrackTarget(scene::EntityHandle entity, StringView componentType,
                                                    StringView propertyPath)
    {
        if (m_scene == nullptr)
        {
            return {};
        }
        if (componentType == kTransformName)
        {
            const propanim::PropertyBinding binding =
                propanim::ResolveBinding(TypeOf<core::Transform>(), propertyPath);
            if (!binding.IsResolved())
            {
                return {};
            }
            core::Transform local = m_scene->GetLocalTransform(entity);
            return propanim::ReadBinding(binding, Instance{&local, &TypeOf<core::Transform>()});
        }
        scene::ComponentManagerBase* mgr = FindManagerByComponentTypeName(componentType);
        if (mgr == nullptr || mgr->ComponentType() == nullptr || !mgr->HasComponent(entity))
        {
            return {};
        }
        const propanim::PropertyBinding binding =
            propanim::ResolveBinding(*mgr->ComponentType(), propertyPath);
        return binding.IsResolved() ? propanim::ReadBinding(binding, mgr->GetComponentInstance(entity))
                                    : Variant{};
    }

    void PropertyAnimationPanel::WriteTrackTarget(scene::EntityHandle entity, StringView componentType,
                                                  StringView propertyPath, const Variant& value)
    {
        if (m_scene == nullptr)
        {
            return;
        }
        if (componentType == kTransformName)
        {
            const propanim::PropertyBinding binding =
                propanim::ResolveBinding(TypeOf<core::Transform>(), propertyPath);
            if (!binding.IsResolved())
            {
                return;
            }
            // Read-modify-write through SetLocalTransform so the world matrix is flagged dirty.
            core::Transform local = m_scene->GetLocalTransform(entity);
            if (propanim::WriteBinding(binding, Instance{&local, &TypeOf<core::Transform>()}, value).IsOk())
            {
                m_scene->SetLocalTransform(entity, local);
            }
            return;
        }
        scene::ComponentManagerBase* mgr = FindManagerByComponentTypeName(componentType);
        if (mgr == nullptr || mgr->ComponentType() == nullptr || !mgr->HasComponent(entity))
        {
            return;
        }
        const propanim::PropertyBinding binding =
            propanim::ResolveBinding(*mgr->ComponentType(), propertyPath);
        if (binding.IsResolved())
        {
            (void)propanim::WriteBinding(binding, mgr->GetComponentInstance(entity), value);
        }
    }

    Array<String> PropertyAnimationPanel::TrackIdentity() const
    {
        Array<String> identity;
        for (const propanim::PropertyTrack& track : m_clip.tracks)
        {
            String key = track.componentType;
            key += u8"|";
            key += track.propertyPath.AsView();
            identity.PushBack(Move(key));
        }
        return identity;
    }

    bool PropertyAnimationPanel::SameIdentity(const Array<String>& a, const Array<String>& b)
    {
        if (a.Size() != b.Size())
        {
            return false;
        }
        for (usize i = 0; i < a.Size(); ++i)
        {
            if (a[i].AsView() != b[i].AsView())
            {
                return false;
            }
        }
        return true;
    }

    void PropertyAnimationPanel::SnapshotEntity(scene::EntityHandle entity)
    {
        m_snapshot.Clear();
        for (const propanim::PropertyTrack& track : m_clip.tracks)
        {
            Variant current =
                ReadTrackTarget(entity, track.componentType.AsView(), track.propertyPath.AsView());
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

    void PropertyAnimationPanel::PreviewAt(scene::EntityHandle entity, f32 time)
    {
        Array<String> identity = TrackIdentity();
        // Re-snapshot when the previewed entity changes OR the track set changed (the #6 fix): the old
        // snapshot describes targets that may no longer exist / may have been retargeted.
        if (!m_previewing || m_previewEntity != entity ||
            !SameIdentity(identity, m_snapshotIdentity))
        {
            if (m_previewing)
            {
                StopPreview(); // restore the old targets before capturing a fresh snapshot
            }
            SnapshotEntity(entity);
            m_snapshotIdentity = Move(identity);
            m_previewing = true;
            m_previewEntity = entity;
        }
        m_previewTime = time;
        for (const propanim::PropertyTrack& track : m_clip.tracks)
        {
            // SampleMerged keeps the live value for empty channels (no teleport-to-origin).
            const Variant current =
                ReadTrackTarget(entity, track.componentType.AsView(), track.propertyPath.AsView());
            WriteTrackTarget(entity, track.componentType.AsView(), track.propertyPath.AsView(),
                             track.SampleMerged(time, current));
        }
    }

    void PropertyAnimationPanel::StopPreview()
    {
        if (!m_previewing)
        {
            return;
        }
        if (m_scene != nullptr && m_previewEntity.IsAssigned())
        {
            for (const PreviewSnapshotEntry& entry : m_snapshot)
            {
                WriteTrackTarget(m_previewEntity, entry.componentType.AsView(),
                                 entry.propertyPath.AsView(), entry.value);
            }
        }
        m_snapshot.Clear();
        m_snapshotIdentity.Clear();
        m_previewing = false;
        m_previewEntity = scene::EntityHandle{};
    }

    void PropertyAnimationPanel::DrawOverlay(foundation::render::debug::DebugDraw& drawList)
    {
        if (!m_previewing || m_scene == nullptr || !m_previewEntity.IsAssigned())
        {
            return;
        }
        // Marker at the previewed entity's WORLD position (overlay so it reads over geometry).
        const Float4x4 world = m_scene->GetWorldMatrix(m_previewEntity);
        const Float3 p{world.m[3][0], world.m[3][1], world.m[3][2]};
        const Color marker{1.0f, 0.85f, 0.2f, 1.0f};
        drawList.DrawWireSphereOverlay(p, 0.35f, marker);
        drawList.DrawText3D(p, u8"preview", marker);
    }
}
