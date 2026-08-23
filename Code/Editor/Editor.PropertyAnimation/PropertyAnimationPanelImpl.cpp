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
namespace scene = foundation::scene;
namespace propanim = foundation::propertyanimation;
namespace ui = foundation::ui;

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

        // Dopesheet lane sizing.
        constexpr f32 kLaneHeight = 22.0f;       // must match DopesheetLane's default height
        constexpr f32 kRulerBand = 24.0f;        // must match Timeline::kRulerHeight
        constexpr f32 kTimelineMinHeight = 28.0f;
        constexpr f32 kTimelineMaxHeight = 220.0f;
        constexpr f32 kTimeEps = 1e-4f;

        // The sorted, de-duplicated key TIMES on a track (a dopesheet marker wherever ANY channel or a
        // quaternion key lands). The lane shows one diamond per distinct time.
        Array<f32> CollectKeyTimes(const propanim::PropertyTrack& track)
        {
            Array<f32> times;
            const auto addTime = [&](f32 t)
            {
                for (const f32 e : times)
                {
                    if (Abs(e - t) < kTimeEps)
                    {
                        return;
                    }
                }
                times.PushBack(t);
            };
            const u32 channelCount = propanim::ChannelCount(track.kind);
            for (u32 c = 0; c < channelCount; ++c)
            {
                for (const CurveKey& k : track.channels[c].Keys())
                {
                    addTime(k.time);
                }
            }
            for (const propanim::QuatKey& q : track.quatKeys)
            {
                addTime(q.time);
            }
            for (usize i = 1; i < times.Size(); ++i) // insertion sort (key counts are small)
            {
                const f32 v = times[i];
                usize j = i;
                while (j > 0 && times[j - 1] > v)
                {
                    times[j] = times[j - 1];
                    --j;
                }
                times[j] = v;
            }
            return times;
        }

        // Retime every key at ~t0 (any channel / quat) to t1, keeping each channel sorted.
        void ShiftTrackKeys(propanim::PropertyTrack& track, f32 t0, f32 t1)
        {
            const u32 channelCount = propanim::ChannelCount(track.kind);
            for (u32 c = 0; c < channelCount; ++c)
            {
                Curve& channel = track.channels[c];
                Array<CurveKey> keys = channel.Keys(); // copy, retime, re-insert sorted
                for (CurveKey& k : keys)
                {
                    if (Abs(k.time - t0) < kTimeEps)
                    {
                        k.time = t1;
                    }
                }
                channel.Clear();
                for (const CurveKey& k : keys)
                {
                    channel.AddKey(k);
                }
            }
            for (propanim::QuatKey& q : track.quatKeys)
            {
                if (Abs(q.time - t0) < kTimeEps)
                {
                    q.time = t1;
                }
            }
            for (usize i = 1; i < track.quatKeys.Size(); ++i)
            {
                const propanim::QuatKey v = track.quatKeys[i];
                usize j = i;
                while (j > 0 && track.quatKeys[j - 1].time > v.time)
                {
                    track.quatKeys[j] = track.quatKeys[j - 1];
                    --j;
                }
                track.quatKeys[j] = v;
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
        RefreshEntitySlot();
        RefreshTransportButtons();
        BuildLanes();
        RefreshClipStateUI(); // no clip yet -> the empty state is the whole panel
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
            lp->Width = ui::SizeSpec::Fixed(ui::Unit::Dp(width));
            lp->Height = ui::SizeSpec::Match();
            row.AddView(button.Get(), lp);
            return button;
        };

        // Header (only shown with a clip loaded - workflow 2026-08-17): document actions +
        // clip label + the BOUND-ENTITY slot. (Collapse/expand is the dock's job.)
        m_header = MakeRef<ui::FlexLayout>(DefaultAllocator());
        m_header->Direction = ui::Orientation::Horizontal;
        m_header->Spacing = 4.0f;
        addButton(*m_header, u8"Create...", 70.0f, [self]() { self->OnCreateClip(); });
        addButton(*m_header, u8"Open...", 62.0f, [self]() { self->OnOpenClip(); });
        addButton(*m_header, u8"Save", 52.0f, [self]() { self->OnSave(); });
        addButton(*m_header, u8"+ Tracks", 72.0f, [self]() { self->OnAddFromSelection(); });
        m_addTrackButton =
            addButton(*m_header, u8"+ Track", 62.0f, [self]() { self->OnAddTrackMenu(); });
        m_clipLabel = MakeRef<ui::Label>(DefaultAllocator(), StringView(u8""));
        m_clipLabel->FontSize.SetValue(11.0f);
        {
            auto lp = MakeRef<ui::FlexLayoutParams>(DefaultAllocator());
            lp->Grow = 1.0f;
            lp->Height = ui::SizeSpec::Match();
            m_header->AddView(m_clipLabel.Get(), lp);
        }
        // The bound-entity slot: preview/keying/seeding target THIS entity, never the live
        // selection - "Use Selected" is where selection enters, "Bind..." opens the scene
        // page's entity picker through the RequestEntityPick seam.
        m_entityLabel = MakeRef<ui::Label>(DefaultAllocator(), StringView(u8""));
        m_entityLabel->FontSize.SetValue(11.0f);
        {
            auto lp = MakeRef<ui::FlexLayoutParams>(DefaultAllocator());
            lp->Width = ui::SizeSpec::Fixed(ui::Unit::Dp(170));
            lp->Height = ui::SizeSpec::Match();
            m_header->AddView(m_entityLabel.Get(), lp);
        }
        addButton(*m_header, u8"Bind...", 58.0f,
                  [self]()
                  {
                      if (!self->RequestEntityPick)
                      {
                          return; // no host wiring (headless) - the slot is read-only then
                      }
                      self->RequestEntityPick(self->m_boundEntity,
                                              [self](const Guid& picked)
                                              {
                                                  if (!picked.IsNil())
                                                  {
                                                      self->BindEntity(picked);
                                                  }
                                              });
                  });
        addButton(*m_header, u8"Use Selected", 96.0f, [self]() { self->BindSelectedEntity(); });
        {
            auto lp = MakeRef<ui::FlexLayoutParams>(DefaultAllocator());
            lp->Width = ui::SizeSpec::Match();
            lp->Height = ui::SizeSpec::Fixed(ui::Unit::Dp(26));
            AddView(m_header.Get(), lp);
        }

        // The EXCLUSIVE empty state: no clip -> just the message + Create/Open (no tracks, no
        // transport, no editing surface - the workflow leaves no gap for silent saves).
        m_emptyState = MakeRef<ui::FlexLayout>(DefaultAllocator());
        m_emptyState->Direction = ui::Orientation::Vertical;
        m_emptyState->Spacing = 8.0f;
        m_emptyState->Padding = ui::Thickness{12, 12};
        {
            auto message = MakeRef<ui::Label>(DefaultAllocator(),
                                              StringView(u8"No animation clip selected for editing."));
            message->FontSize.SetValue(12.0f);
            auto lp = MakeRef<ui::FlexLayoutParams>(DefaultAllocator());
            lp->Width = ui::SizeSpec::Match();
            m_emptyState->AddView(message.Get(), lp);
        }
        {
            auto row = MakeRef<ui::FlexLayout>(DefaultAllocator());
            row->Direction = ui::Orientation::Horizontal;
            row->Spacing = 6.0f;
            addButton(*row, u8"Create Clip...", 104.0f, [self]() { self->OnCreateClip(); });
            addButton(*row, u8"Open Clip...", 96.0f, [self]() { self->OnOpenClip(); });
            auto lp = MakeRef<ui::FlexLayoutParams>(DefaultAllocator());
            lp->Width = ui::SizeSpec::Match();
            lp->Height = ui::SizeSpec::Fixed(ui::Unit::Dp(26));
            m_emptyState->AddView(row.Get(), lp);
        }
        {
            auto lp = MakeRef<ui::FlexLayoutParams>(DefaultAllocator());
            lp->Width = ui::SizeSpec::Match();
            AddView(m_emptyState.Get(), lp);
        }

        // Body: [ transport | Timeline scrubber | shared ClipEditorView ]. Hidden when collapsed.
        m_body = MakeRef<ui::FlexLayout>(DefaultAllocator());
        m_body->Direction = ui::Orientation::Vertical;
        m_body->Spacing = 4.0f;

        // Transport row: Play / Pause / Stop / Loop toggle.
        auto transport = MakeRef<ui::FlexLayout>(DefaultAllocator());
        transport->Direction = ui::Orientation::Horizontal;
        transport->Spacing = 4.0f;
        m_playButton = addButton(*transport, u8"Play", 52.0f, [self]() { self->Play(); });
        m_pauseButton = addButton(*transport, u8"Pause", 60.0f, [self]() { self->TogglePause(); });
        addButton(*transport, u8"Stop", 52.0f, [self]() { self->Stop(); });
        m_loopButton =
            addButton(*transport, u8"Loop: on", 76.0f, [self]() { self->SetLooping(!self->m_loop); });
        {
            auto lp = MakeRef<ui::FlexLayoutParams>(DefaultAllocator());
            lp->Width = ui::SizeSpec::Match();
            lp->Height = ui::SizeSpec::Fixed(ui::Unit::Dp(26));
            m_body->AddView(transport.Get(), lp);
        }

        m_timeline = MakeRef<ui::toolkit::Timeline>(DefaultAllocator());
        m_timeline->SetDuration(Max(Max(m_clip.duration, m_clip.ComputeDuration()), 1.0f));
        m_timeline->LabelColumnWidth = 140.0f; // the track-label gutter (the dopesheet is the track list)
        // The scrubber routes through OnScrubTimeChanged, which ignores it while Playing (D6). A key
        // drag on a lane commits through MoveSelectedKeys (drags-are-visual, one undo step - D4).
        m_timeline->OnPlayheadMoved.Add([self](f32 t) { self->OnScrubTimeChanged(t); });
        m_timeline->OnKeysMoved.Add([self](f32 d) { self->MoveSelectedKeys(d); });
        // D1: when the dopesheet zooms/scrolls, push the new transform into the curve canvas so it
        // follows and stays aligned under the lanes.
        m_timeline->OnViewChanged.Add([self]() { if (self->m_view) self->m_view->SyncCanvasTransform(); });
        // A gutter/label (or key) pick selects the TRACK: the strip, inspector and canvas below
        // re-target (Sedulous shape).
        m_timeline->OnLaneSelected.Add(
            [self](i32 lane)
            {
                if (self->m_view)
                {
                    self->m_view->SetSelectedTrack(lane);
                }
            });
        // A dopesheet pick updates the value readout too (selection is selection wherever it
        // happens). Lane == track; a diamond merges channels, so channel -1 = the whole track
        // at that key time. Multi-select shows the LAST-reported ref; empty clears.
        m_timeline->OnSelectionChanged.Add(
            [self]()
            {
                if (!self->m_view)
                {
                    return;
                }
                const Array<ui::toolkit::DopesheetKeyRef> sel = self->m_timeline->Selection();
                if (sel.IsEmpty())
                {
                    self->m_view->ClearSelectedKey();
                    return;
                }
                const ui::toolkit::DopesheetKeyRef& r = sel[sel.Size() - 1];
                if (r.lane < self->m_laneKeyTimes.Size() &&
                    r.index < self->m_laneKeyTimes[r.lane].Size())
                {
                    self->m_view->ShowSelectedKey(r.lane, /*channel=*/-1,
                                                  self->m_laneKeyTimes[r.lane][r.index]);
                }
            });
        {
            m_timelineParams = MakeRef<ui::FlexLayoutParams>(DefaultAllocator());
            m_timelineParams->Width = ui::SizeSpec::Match();
            m_timelineParams->Height = ui::SizeSpec::Fixed(ui::Unit::Dp(kTimelineMinHeight));
            m_body->AddView(m_timeline.Get(), m_timelineParams);
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
        // The header only shows with a clip loaded (the empty state owns the no-clip case).
        String text;
        if (HasClip())
        {
            text = String(u8"Clip: ");
            text += ClipName();
            if (m_dirty)
            {
                text += u8" *";
            }
        }
        if (!m_statusFlash.IsEmpty())
        {
            text += u8"   [";
            text += m_statusFlash.AsView();
            text += u8"]";
        }
        if (m_clipLabel.Get() != nullptr)
        {
            m_clipLabel->SetText(text.AsView());
        }
    }

    void PropertyAnimationPanel::FlashStatus(StringView status)
    {
        m_statusFlash = String(status);
        m_statusFlashSeconds = 2.5f;
        RefreshHeader();
    }

    // === header actions ===

    void PropertyAnimationPanel::RunDirtyGuarded(Function<void()> proceed)
    {
        if (!proceed)
        {
            return;
        }
        if (!HasClip() || !m_dirty)
        {
            proceed();
            return;
        }
        ui::UIContext* ctx = this->Context;
        if (ctx == nullptr)
        {
            proceed(); // headless (tests): no dialog host to ask - the caller decided
            return;
        }
        String message = String(u8"Clip '");
        message += m_clipName.AsView();
        message += u8"' has unsaved changes.";
        const StringView choices[] = {u8"Save", u8"Discard", u8"Cancel"};
        auto dialog =
            MakeRef<app::ConfirmDialog>(DefaultAllocator(), StringView(u8"Unsaved Clip"),
                                        message.AsView(), Span<const StringView>(choices, 3));
        PropertyAnimationPanel* self = this;
        dialog->OnChosen = [self, fn = Move(proceed)](usize choice)
        {
            if (choice == 2)
            {
                return; // Cancel: keep editing the current clip
            }
            if (choice == 0)
            {
                self->SaveClip();
                if (self->m_dirty)
                {
                    return; // the save FAILED (header flashed why) - don't lose the edits too
                }
            }
            fn();
        };
        dialog->Show(ctx);
    }

    void PropertyAnimationPanel::OnCreateClip()
    {
        PropertyAnimationPanel* self = this;
        RunDirtyGuarded(
            [self]()
            {
                ui::UIContext* ctx = self->Context;
                if (ctx == nullptr || self->m_editorCtx->Project() == nullptr)
                {
                    return;
                }
                auto dialog = MakeRef<app::AssetCreateDialog>(DefaultAllocator(),
                                                              *self->m_editorCtx,
                                                              u8"Create Animation Clip",
                                                              u8"clip name");
                dialog->OnCreate =
                    [self](foundation::content::Group& group, StringView name)
                {
                    foundation::content::Instance* inst =
                        CreatePropertyAnimationClipNamed(*self->m_editorCtx, group, name);
                    if (inst == nullptr)
                    {
                        LOG_WARNING(u8"PropertyAnimation", u8"Create clip '{}' failed", name);
                        return;
                    }
                    self->LoadClip(inst->Id()); // the guid is the open/identity currency
                };
                dialog->Show(ctx);
            });
    }

    void PropertyAnimationPanel::OnOpenClip()
    {
        PropertyAnimationPanel* self = this;
        RunDirtyGuarded(
            [self]()
            {
                ui::UIContext* ctx = self->Context;
                if (ctx == nullptr || self->m_editorCtx->Project() == nullptr)
                {
                    return;
                }
                Array<String> types;
                types.PushBack(String(u8"PropertyAnimationClipAsset"));
                auto dialog = MakeRef<app::AssetPickerDialog>(DefaultAllocator(),
                                                              *self->m_editorCtx, Move(types));
                dialog->OnPicked = [self](const Guid& picked)
                {
                    if (!picked.IsNil())
                    {
                        self->LoadClip(picked);
                    }
                };
                dialog->Show(ctx);
            });
    }

    void PropertyAnimationPanel::RequestEditClip(const Guid& clipId, const Guid& bindEntity)
    {
        if (clipId.IsNil())
        {
            return;
        }
        if (m_clipId == clipId)
        {
            // Already the open document (dirty or not) - just honor the binding request.
            if (!bindEntity.IsNil())
            {
                BindEntity(bindEntity);
            }
            return;
        }
        PropertyAnimationPanel* self = this;
        const Guid clip = clipId;
        const Guid entity = bindEntity;
        RunDirtyGuarded(
            [self, clip, entity]()
            {
                self->LoadClip(clip);
                if (self->m_clipId == clip && !entity.IsNil())
                {
                    self->BindEntity(entity); // the pencil's animator entity auto-binds
                }
            });
    }

    // === the bound entity ===

    void PropertyAnimationPanel::BindEntity(const Guid& entityId)
    {
        if (m_boundEntity == entityId)
        {
            return;
        }
        StopPreview(); // the preview snapshot belongs to the OLD binding - restore it first
        m_boundEntity = entityId;
        RefreshEntitySlot();
    }

    void PropertyAnimationPanel::BindSelectedEntity()
    {
        const Guid* primary = (m_selection != nullptr) ? m_selection->Primary() : nullptr;
        if (primary == nullptr)
        {
            FlashStatus(u8"bind: nothing selected");
            return;
        }
        BindEntity(*primary);
    }

    void PropertyAnimationPanel::RefreshEntitySlot()
    {
        if (m_entityLabel.Get() == nullptr)
        {
            return;
        }
        String text = String(u8"Entity: ");
        if (m_boundEntity.IsNil())
        {
            text += u8"(none)";
        }
        else
        {
            const scene::EntityHandle entity =
                (m_scene != nullptr) ? m_scene->FindEntity(m_boundEntity) : scene::EntityHandle{};
            if (!entity.IsAssigned())
            {
                text += u8"(missing)"; // bound guid no longer resolves in this scene
            }
            else
            {
                const String name = String(m_scene->GetEntityName(entity));
                text += name.IsEmpty() ? StringView(u8"(unnamed)") : name.AsView();
            }
        }
        m_entityLabel->SetText(text.AsView());
    }

    void PropertyAnimationPanel::RefreshClipStateUI()
    {
        const bool hasClip = HasClip();
        if (m_header.Get() != nullptr)
        {
            m_header->Visibility = hasClip ? ui::Visibility::Visible : ui::Visibility::Gone;
        }
        if (m_body.Get() != nullptr)
        {
            m_body->Visibility = hasClip ? ui::Visibility::Visible : ui::Visibility::Gone;
        }
        if (m_emptyState.Get() != nullptr)
        {
            m_emptyState->Visibility = hasClip ? ui::Visibility::Gone : ui::Visibility::Visible;
        }
        Invalidate();
    }

    void PropertyAnimationPanel::OnAddFromSelection()
    {
        const usize added = AddTracksFromSelection(*m_view);
        if (added == 0)
        {
            LOG_INFO(u8"Editor",
                     u8"add-tracks: no bound entity or no animatable properties");
        }
        RefreshHeader(); // reflect the new track count (the label showed a stale "none")
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
        m_playing = false; // a new document starts stopped at the top
        m_paused = false;
        m_playheadTime = 0.0f;
        if (m_timeline.Get() != nullptr)
        {
            m_timeline->SetPlayheadTime(0.0f);
        }
        RefreshTransportButtons();
        m_clip = propanim::PropertyAnimationClip{};
        m_clipId = Guid{};
        m_clipName = String{};
        m_dirty = false;
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
        // LoadClip owns the whole refresh (callers used to repeat it): rows, header, and the
        // empty-state <-> editor swap.
        if (m_view)
        {
            m_view->ResetForClip();
        }
        RefreshHeader();
        RefreshClipStateUI();
    }

    void PropertyAnimationPanel::SaveClip()
    {
        // Every outcome is VISIBLE (UAT: "if it saves, there is no feedback") - the header
        // flashes the result, and no early-out is silent.
        if (m_clipId.IsNil() || m_editorCtx->Project() == nullptr)
        {
            LOG_WARNING(u8"PropertyAnimation",
                        u8"Save: no clip asset loaded - use Create or Open first");
            FlashStatus(u8"save: no clip loaded");
            return;
        }
        foundation::content::Instance* inst = m_editorCtx->Project()->SourceDb().GetInstance(m_clipId);
        if (inst == nullptr)
        {
            LOG_WARNING(u8"PropertyAnimation",
                        u8"Save: the clip asset {} no longer exists in the project", m_clipId);
            FlashStatus(u8"save FAILED: asset missing");
            return;
        }
        pipeline::PropertyAnimationClipAsset asset;
        propanim::PropertyAnimationClipSource::FromClip(m_clip, asset.source);
        if (inst->WriteObject(asset).IsOk())
        {
            m_dirty = false;
            m_editorCtx->RequestCook(false);
            LOG_INFO(u8"Editor", u8"saved in-scene property-animation clip '{}'", m_clipName);
            FlashStatus(u8"saved"); // RefreshHeader inside also clears the dirty star
        }
        else
        {
            LOG_WARNING(u8"PropertyAnimation", u8"Save: writing clip '{}' failed", m_clipName);
            FlashStatus(u8"save FAILED (see log)");
        }
    }

    Array<AnimatablePropertyInfo> PropertyAnimationPanel::CollectSelectionTrackSeeds()
    {
        Array<AnimatablePropertyInfo> seeds;
        if (m_scene == nullptr || m_boundEntity.IsNil())
        {
            return seeds;
        }
        const scene::EntityHandle entity = m_scene->FindEntity(m_boundEntity);
        if (!entity.IsAssigned())
        {
            return seeds;
        }
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
        return seeds;
    }

    bool PropertyAnimationPanel::ClipHasTrack(StringView componentType, StringView propertyPath) const
    {
        for (const propanim::PropertyTrack& t : m_clip.tracks)
        {
            if (t.componentType.AsView() == componentType && t.propertyPath.AsView() == propertyPath)
            {
                return true;
            }
        }
        return false;
    }

    usize PropertyAnimationPanel::AddTracksFromSelection(ClipEditorView& view)
    {
        Array<AnimatablePropertyInfo> seeds = CollectSelectionTrackSeeds();
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

    void PropertyAnimationPanel::OnAddTrackMenu()
    {
        ui::UIContext* ctx = this->Context;
        if (ctx == nullptr)
        {
            return;
        }
        const Array<AnimatablePropertyInfo> seeds = CollectSelectionTrackSeeds();
        auto menu = MakeRef<ui::ContextMenu>(DefaultAllocator());
        PropertyAnimationPanel* self = this;
        usize offered = 0;
        for (const AnimatablePropertyInfo& seed : seeds)
        {
            if (ClipHasTrack(seed.componentType.AsView(), seed.propertyPath.AsView()))
            {
                continue; // already a track for this property - don't offer a duplicate
            }
            String label = seed.componentType;
            label += u8".";
            label += seed.propertyPath.AsView();
            const AnimatablePropertyInfo pick = seed; // copy for the click closure
            menu->AddItem(label.AsView(),
                          [self, pick]()
                          {
                              self->m_view->AddTrack(pick.componentType.AsView(),
                                                     pick.propertyPath.AsView(), pick.kind);
                              self->RefreshHeader();
                          });
            ++offered;
        }
        if (offered == 0)
        {
            menu->AddItem(seeds.IsEmpty() ? StringView(u8"(bind an entity first)")
                                          : StringView(u8"(all properties already tracked)"),
                          []() {});
        }
        const Float2 pos = (m_addTrackButton.Get() != nullptr)
                               ? m_addTrackButton->LocalToScreen(Float2{0.0f, 24.0f})
                               : Float2{0.0f, 0.0f};
        menu->Show(ctx, pos.x, pos.y);
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

    void PropertyAnimationPanel::Tick(f32 dt, bool editingLocked)
    {
        if (m_statusFlashSeconds > 0.0f)
        {
            m_statusFlashSeconds -= (dt > 0.0f) ? dt : 0.0f;
            if (m_statusFlashSeconds <= 0.0f)
            {
                m_statusFlash = String();
                RefreshHeader(); // restore the plain clip line
            }
        }
        m_editingLocked = editingLocked;
        if (m_editingLocked)
        {
            StopPlaybackInternal(); // no editor playback under Simulate/Play
            StopPreview();          // restore + stand the preview down
            return;
        }
        if (m_playing && !m_paused)
        {
            Advance(dt); // only Playing mutates widgets -> idle panel = zero redraws (A6)
        }
    }

    // === transport (D6: Editing | Playing) ===

    void PropertyAnimationPanel::Play()
    {
        if (m_clip.tracks.IsEmpty())
        {
            return;
        }
        const f32 dur = Max(Max(m_clip.duration, m_clip.ComputeDuration()), 1e-3f);
        if (m_playheadTime >= dur - 1e-4f)
        {
            m_playheadTime = 0.0f; // restart from the top if parked at the end
        }
        m_playing = true;
        m_paused = false;
        RefreshTransportButtons();
    }

    void PropertyAnimationPanel::TogglePause()
    {
        if (!m_playing)
        {
            return; // pause only means something while Playing
        }
        m_paused = !m_paused;
        RefreshTransportButtons();
    }

    void PropertyAnimationPanel::Stop()
    {
        m_playing = false;
        m_paused = false;
        m_playheadTime = 0.0f;
        if (m_timeline.Get() != nullptr)
        {
            m_timeline->SetPlayheadTime(0.0f);
        }
        if (m_view)
        {
            m_view->SetScrubTime(0.0f);
        }
        PreviewSelected(0.0f); // show the start pose
        RefreshTransportButtons();
    }

    void PropertyAnimationPanel::StopPlaybackInternal()
    {
        if (!m_playing && !m_paused)
        {
            return;
        }
        m_playing = false;
        m_paused = false;
        RefreshTransportButtons();
    }

    void PropertyAnimationPanel::SetLooping(bool loop)
    {
        m_loop = loop;
        RefreshTransportButtons();
    }

    void PropertyAnimationPanel::Advance(f32 dt)
    {
        if (m_clip.tracks.IsEmpty())
        {
            Stop();
            return;
        }
        const f32 dur = Max(Max(m_clip.duration, m_clip.ComputeDuration()), 1e-3f);
        m_playheadTime += (dt > 0.0f) ? dt : 0.0f;
        if (m_playheadTime >= dur)
        {
            if (m_loop)
            {
                while (m_playheadTime >= dur)
                {
                    m_playheadTime -= dur; // wrap (dt << dur, so a plain subtract loop is enough)
                }
            }
            else
            {
                m_playheadTime = dur; // clamp + stop at the end
                m_playing = false;
                m_paused = false;
                RefreshTransportButtons();
            }
        }
        if (m_timeline.Get() != nullptr)
        {
            m_timeline->SetPlayheadTime(m_playheadTime); // the scrubber event is guarded while playing
        }
        if (m_view)
        {
            m_view->SetScrubTime(m_playheadTime);
        }
        PreviewSelected(m_playheadTime); // the advance loop owns the playhead -> preview directly (D6)
    }

    void PropertyAnimationPanel::RefreshTransportButtons()
    {
        if (m_pauseButton.Get() != nullptr)
        {
            m_pauseButton->SetText(m_paused ? StringView(u8"Resume") : StringView(u8"Pause"));
        }
        if (m_loopButton.Get() != nullptr)
        {
            m_loopButton->SetText(m_loop ? StringView(u8"Loop: on") : StringView(u8"Loop: off"));
        }
    }

    void PropertyAnimationPanel::OnScrubTimeChanged(f32 time)
    {
        if (m_playing && !m_paused)
        {
            return; // D6: active playback owns the playhead; a scrub is ignored
        }
        m_playheadTime = time; // a scrub (Editing or Paused) repositions the transport clock
        if (m_view)
        {
            m_view->SetScrubTime(time); // keep the sampled-value readout in step with the scrubber
        }
        PreviewSelected(time);
    }

    void PropertyAnimationPanel::PreviewSelected(f32 time)
    {
        if (m_clip.tracks.IsEmpty() || m_editingLocked || m_scene == nullptr)
        {
            return;
        }
        if (m_boundEntity.IsNil())
        {
            StopPreview();
            return;
        }
        const scene::EntityHandle entity = m_scene->FindEntity(m_boundEntity);
        if (!entity.IsAssigned())
        {
            StopPreview();
            return;
        }
        PreviewAt(entity, time);
    }

    void PropertyAnimationPanel::OnClipViewRebuilt()
    {
        // The clip length / track set may have changed (a Length edit, add/remove track, undo/redo):
        // resync the Timeline axis to the AUTHORED duration + rebuild the dopesheet lanes (selection
        // preserved by time).
        if (m_timeline.Get() != nullptr)
        {
            m_timeline->SetDuration(Max(Max(m_clip.duration, m_clip.ComputeDuration()), 1.0f));
        }
        BuildLanes();
    }

    void PropertyAnimationPanel::SetClipDuration(f32 seconds)
    {
        if (!m_view)
        {
            return;
        }
        propanim::PropertyAnimationClip before = m_clip;
        propanim::PropertyAnimationClip after = m_clip;
        // Never below the last key (a duration can't cut a key off); PushClipEdit re-applies this max.
        after.duration = Max(seconds, after.ComputeDuration());
        if (after.duration == before.duration)
        {
            return; // no change
        }
        m_view->PushClipEdit(Move(before), Move(after));
    }

    IClipEditorHost::TimeAxis PropertyAnimationPanel::ClipTimeTransform() const
    {
        IClipEditorHost::TimeAxis axis;
        if (m_timeline.Get() != nullptr)
        {
            axis.pixelsPerSecond = m_timeline->PixelsPerSecond();
            axis.scrollSeconds = m_timeline->ScrollSeconds();
            axis.labelColumnWidth = m_timeline->LabelColumnWidth; // the 140px dopesheet gutter
        }
        return axis;
    }

    void PropertyAnimationPanel::BuildLanes()
    {
        if (m_timeline.Get() == nullptr)
        {
            return;
        }

        // Preserve the selection across the rebuild by TIME (keys have no id - D3/D4). A pending move
        // overrides with the moved keys' NEW times; otherwise re-capture the current selection's times.
        Array<ReselectMark> marks;
        if (m_haveReselect)
        {
            marks = Move(m_reselectTimes);
            m_reselectTimes = Array<ReselectMark>{};
            m_haveReselect = false;
        }
        else
        {
            for (const ui::toolkit::DopesheetKeyRef& r : m_timeline->Selection())
            {
                if (r.lane < m_laneKeyTimes.Size() && r.index < m_laneKeyTimes[r.lane].Size())
                {
                    marks.PushBack(ReselectMark{r.lane, m_laneKeyTimes[r.lane][r.index]});
                }
            }
        }

        // One lane per track; markers at the track's distinct key times.
        Array<ui::toolkit::DopesheetLane> lanes;
        m_laneKeyTimes.Clear();
        for (const propanim::PropertyTrack& track : m_clip.tracks)
        {
            Array<f32> times = CollectKeyTimes(track);
            ui::toolkit::DopesheetLane lane;
            String label = track.componentType;
            label += u8".";
            label += track.propertyPath.AsView();
            lane.label = Move(label);
            lane.keyTimes = times;
            lanes.PushBack(Move(lane));
            m_laneKeyTimes.PushBack(Move(times));
        }
        m_timeline->SetLanes(Move(lanes)); // clears the widget's selection
        // The dopesheet IS the track list: mirror the view's selected track into the lane
        // highlight (programmatic - no event loop).
        m_timeline->SetSelectedLane(m_view ? m_view->SelectedTrack() : -1);

        // Re-resolve the selection by time against the rebuilt lanes.
        Array<ui::toolkit::DopesheetKeyRef> newSel;
        for (const ReselectMark& mark : marks)
        {
            if (mark.lane >= m_laneKeyTimes.Size())
            {
                continue;
            }
            const Array<f32>& lt = m_laneKeyTimes[mark.lane];
            for (usize i = 0; i < lt.Size(); ++i)
            {
                if (Abs(lt[i] - mark.time) < kTimeEps)
                {
                    newSel.PushBack(ui::toolkit::DopesheetKeyRef{mark.lane, static_cast<u32>(i)});
                    break;
                }
            }
        }
        m_timeline->SetSelection(newSel);

        // Size the timeline pane to the ruler + lanes (capped; row virtualization/scroll is A9-deferred).
        const f32 h = Clamp(kRulerBand + static_cast<f32>(m_clip.tracks.Size()) * kLaneHeight,
                            kTimelineMinHeight, kTimelineMaxHeight);
        if (m_timelineParams.Get() != nullptr)
        {
            m_timelineParams->Height = ui::SizeSpec::Fixed(ui::Unit::Dp(h));
        }
        if (m_body.Get() != nullptr)
        {
            m_body->Invalidate();
        }
    }

    void PropertyAnimationPanel::MoveSelectedKeys(f32 deltaSeconds)
    {
        if (!m_view || m_timeline.Get() == nullptr || Abs(deltaSeconds) < 1e-5f)
        {
            return;
        }
        const Array<ui::toolkit::DopesheetKeyRef> sel = m_timeline->Selection();
        if (sel.IsEmpty())
        {
            return;
        }

        propanim::PropertyAnimationClip before = m_clip;
        propanim::PropertyAnimationClip after = m_clip;

        Array<ReselectMark> marks;
        for (const ui::toolkit::DopesheetKeyRef& r : sel)
        {
            if (r.lane >= after.tracks.Size() || r.lane >= m_laneKeyTimes.Size() ||
                r.index >= m_laneKeyTimes[r.lane].Size())
            {
                continue;
            }
            const f32 t0 = m_laneKeyTimes[r.lane][r.index];
            const f32 t1 = Max(t0 + deltaSeconds, 0.0f);
            ShiftTrackKeys(after.tracks[r.lane], t0, t1);
            marks.PushBack(ReselectMark{r.lane, t1});
        }
        if (marks.IsEmpty())
        {
            return;
        }

        m_reselectTimes = Move(marks);
        m_haveReselect = true;
        m_view->PushClipEdit(Move(before), Move(after)); // one undo step; applies + defers a row rebuild
        BuildLanes(); // rebuild lanes now + re-select the moved keys by their new time (consumes pending)
    }

    void PropertyAnimationPanel::ApplyClipState(const propanim::PropertyAnimationClip& state,
                                                bool rebuild)
    {
        // Forward to the persistent view (sets Clip() = state + rebuilds). The command held THIS host,
        // never the view, so the view being recreatable can never dangle a command (F1).
        if (m_view)
        {
            m_view->ApplyState(state, rebuild);
        }
        else
        {
            m_clip = state;
        }
    }

    Variant PropertyAnimationPanel::ReadSceneValue(StringView componentType,
                                                   StringView propertyPath)
    {
        // Every failure names ITS stage: "no scene value" alone cost a live session to
        // diagnose - the chain has five distinct ways to miss.
        if (m_scene == nullptr)
        {
            LOG_WARNING(u8"PropertyAnimation", u8"Key capture: the panel has no scene");
            return {};
        }
        if (m_boundEntity.IsNil())
        {
            LOG_WARNING(u8"PropertyAnimation",
                        u8"Key capture: no entity bound (use Bind... or Use Selected)");
            return {};
        }
        const scene::EntityHandle entity = m_scene->FindEntity(m_boundEntity);
        if (!entity.IsAssigned())
        {
            LOG_WARNING(u8"PropertyAnimation",
                        u8"Key capture: the bound guid {} is not an entity of scene '{}'",
                        m_boundEntity, m_scene->Name());
            return {};
        }
        Variant value = ReadTrackTarget(entity, componentType, propertyPath);
        if (value.IsEmpty())
        {
            // Re-derive the reason ReadTrackTarget (shared with preview - silent by design)
            // came back empty.
            if (componentType == kTransformName)
            {
                LOG_WARNING(u8"PropertyAnimation",
                            u8"Key capture: property path '{}' did not resolve on the "
                            u8"Transform (or its reflection is unregistered)",
                            propertyPath);
            }
            else if (scene::ComponentManagerBase* mgr =
                         FindManagerByComponentTypeName(componentType);
                     mgr == nullptr)
            {
                LOG_WARNING(u8"PropertyAnimation",
                            u8"Key capture: no component manager named '{}' on this scene",
                            componentType);
            }
            else if (!mgr->HasComponent(entity))
            {
                LOG_WARNING(u8"PropertyAnimation",
                            u8"Key capture: the bound entity has no '{}' component",
                            componentType);
            }
            else
            {
                LOG_WARNING(u8"PropertyAnimation",
                            u8"Key capture: property path '{}' did not resolve on '{}'",
                            propertyPath, componentType);
            }
        }
        return value;
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
