// SPDX-License-Identifier: MIT
// Copyright (c) 2026-Present Robert Campbell

// Editor::Scene - :animation_clip_page partition (implementation).

module;
#include <cmath> // std::fmod (looping playhead wrap)
#include "Core/Prelude.h"
#include "Core/Log/Log.h"
#include "Core/Reflection/Reflect.h"

module editor.scene;

import foundation.core;
import foundation.settings; // per-project editor-settings store (preview skeleton/mesh prefs)
import foundation.content;
import foundation.rhi;
import foundation.graphics;
import foundation.shell;
import foundation.runtime;
import foundation.runtime.client;
import foundation.scene;
import engine.scene;
import foundation.animation;
import foundation.animation.resource;
import animation.pipeline;
import foundation.resource;
import foundation.render;
import engine.render;
import foundation.ui;
import foundation.ui.toolkit;
import foundation.ui.runtime;
import foundation.ui.viewport;
import foundation.vg.renderer;
import editor.core;
import editor.app;
import editor.preview;
import :animation_graph_page; // DrawSkeletonWireframe (shared preview helper, impl-only)

using namespace foundation::core;
namespace animation = foundation::animation;
namespace render = foundation::render;
namespace rhi = foundation::rhi;
namespace runtime = foundation::runtime;
namespace ui = foundation::ui;
namespace vg = foundation::vg;
namespace fonts = foundation::fonts;

namespace editor
{
    // "Prefix: <asset name>" (or (none)/(missing)) - the transport button labels.
    static String ClipPickLabel(EditorContext& ctx, StringView prefix, const Guid& id)
    {
        String label(prefix);
        if (ctx.Project() != nullptr && !id.IsNil())
        {
            foundation::content::Instance* inst = ctx.Project()->SourceDb().GetInstance(id);
            label.Append(inst != nullptr ? inst->Name() : StringView(u8"(missing)"));
        }
        else
        {
            label.Append(u8"(none)");
        }
        return label;
    }

    // Per-asset clip-preview prefs: {clipGuid -> (skeleton guid, skinned-mesh guid)} - a section in
    // the per-project editor-settings store, so a reopened clip viewer restores its preview rig.
    struct ClipPreviewPref
    {
        Guid asset;
        Guid skeleton;
        Guid mesh;
        void Serialize(ISerializer& ar)
        {
            ar.Key("asset");
            ar.GuidValue(asset);
            ar.Key("skeleton");
            ar.GuidValue(skeleton);
            ar.Key("mesh");
            ar.GuidValue(mesh);
        }
    };
    inline void Serialize(ISerializer& ar, ClipPreviewPref& p)
    {
        ar.BeginObject();
        p.Serialize(ar);
        ar.EndObject();
    }
    class ClipPreviewSettings final : public ISerializable
    {
        RTTI_OBJECT(ClipPreviewSettings, ISerializable)
    public:
        Array<ClipPreviewPref> prefs;
        void Serialize(ISerializer& ar) override
        {
            foundation::core::Serialize(ar, "prefs", prefs);
        }
    };

    // ============================ Construction ==============================================

    AnimationClipEditorPage::AnimationClipEditorPage(EditorContext& context,
                                                     runtime::IApplicationHost& host,
                                                     ui::runtime::UIHost& uiHost,
                                                     foundation::content::Instance& instance)
        : m_context(&context), m_host(&host), m_uiHost(&uiHost), m_title(instance.Name())
    {
        // Shared preview substrate (viewport + preview scene + orbit camera + render loop).
        m_preview =
            MakeUnique<PreviewViewport>(foundation::core::DefaultAllocator(), host, uiHost, u8"animclip.preview");
        m_preview->SetClearColor(Color{0.05f, 0.05f, 0.07f, 1.0f});
        m_preview->Camera().position = Float3{0.0f, 1.4f, 3.2f};
        m_preview->Camera().LookAt(Float3{0.0f, 0.9f, 0.0f});

        SetInstanceId(instance.Id());

        RefPtr<ISerializable> object = instance.ReadObject();
        m_asset = RefPtr<pipeline::AnimationClipAsset>(
            Cast<pipeline::AnimationClipAsset>(object.Get()));
        if (m_asset.Get() == nullptr)
        {
            LOG_ERROR(u8"Editor",
                               u8"animation clip '{}' failed to read - page opens empty", m_title);
        }
        m_undoBaseline = SnapshotAsset();
        if (m_context->Resources() != nullptr)
        {
            m_clip = m_context->Resources()->Bind<animation::AnimationClip>(InstanceId());
        }

        BuildPreviewScene();

        // Transport: skeleton pick + play/pause + a normalized scrub slider + time readout.
        auto transport = MakeRef<ui::FlexLayout>(foundation::core::DefaultAllocator());
        transport->Direction = ui::Orientation::Horizontal;
        transport->Spacing = 6.0f;
        transport->Padding = ui::Thickness{6, 4};
        {
            AnimationClipEditorPage* self = this;
            m_skeletonButton =
                MakeRef<ui::Button>(foundation::core::DefaultAllocator(), StringView(u8"Skeleton: (none)"));
            m_skeletonButton->OnClick.Add([self](ui::ButtonBase*) { self->PickPreviewSkeleton(); });
            transport->AddView(m_skeletonButton.Get());
            m_meshButton = MakeRef<ui::Button>(foundation::core::DefaultAllocator(), StringView(u8"Mesh: (none)"));
            m_meshButton->OnClick.Add([self](ui::ButtonBase*) { self->PickPreviewMesh(); });
            transport->AddView(m_meshButton.Get());
            m_playButton = MakeRef<ui::Button>(foundation::core::DefaultAllocator(), StringView(u8"Pause"));
            m_playButton->OnClick.Add(
                [self](ui::ButtonBase*)
                {
                    self->m_playing = !self->m_playing;
                    self->m_playButton->SetText(self->m_playing ? StringView(u8"Pause")
                                                                : StringView(u8"Play"));
                });
            transport->AddView(m_playButton.Get());

            m_timeSlider = MakeRef<ui::Slider>(foundation::core::DefaultAllocator());
            m_timeSlider->Min.SetValue(0.0f);
            m_timeSlider->Max.SetValue(1.0f);
            m_timeSlider->OnValueChanged.Add(
                [self](ui::Slider*, f32 v)
                {
                    if (self->m_scrubbing)
                    {
                        return; // playback echo
                    }
                    animation::AnimationClip* clip = self->m_clip.Get();
                    if (clip != nullptr && clip->duration > 0.0f)
                    {
                        self->m_time = v * clip->duration;
                        self->m_playing = false;
                        self->m_playButton->SetText(u8"Play");
                    }
                });
            auto slp = MakeRef<ui::FlexLayoutParams>(foundation::core::DefaultAllocator());
            slp->Grow = 1.0f;
            transport->AddView(m_timeSlider.Get(), slp);

            m_timeLabel = MakeRef<ui::Label>(foundation::core::DefaultAllocator());
            m_timeLabel->FontSize.SetValue(Optional<f32>{12.0f});
            m_timeLabel->VAlign.SetValue(fonts::VerticalAlignment::Middle);
            auto tlp = MakeRef<ui::FlexLayoutParams>(foundation::core::DefaultAllocator());
            tlp->Width = ui::SizeSpec::Fixed(ui::Unit::Dp(110.0f));
            transport->AddView(m_timeLabel.Get(), tlp);
        }

        auto previewColumn = MakeRef<ui::FlexLayout>(foundation::core::DefaultAllocator());
        previewColumn->Direction = ui::Orientation::Vertical;
        {
            auto lp = MakeRef<ui::FlexLayoutParams>(foundation::core::DefaultAllocator());
            lp->Width = ui::SizeSpec::Match();
            previewColumn->AddView(transport.Get(), lp);
            auto grow = MakeRef<ui::FlexLayoutParams>(foundation::core::DefaultAllocator());
            grow->Grow = 1.0f;
            grow->Width = ui::SizeSpec::Match();
            previewColumn->AddView(m_preview->View(), grow);
        }

        m_grid = MakeRef<ui::toolkit::PropertyGrid>(foundation::core::DefaultAllocator());
        RebuildGrid();

        auto split = MakeRef<ui::toolkit::SplitView>(foundation::core::DefaultAllocator());
        split->SetSplitRatio(0.66f);
        split->SetPanes(previewColumn.Get(), m_grid.Get());
        m_content = split;

        // Restore the persisted preview rig (skeleton + skinned mesh) for this clip.
        LoadPreviewPref();
    }

    void AnimationClipEditorPage::LoadPreviewPref()
    {
        foundation::settings::Settings* store = m_context->ProjectEditorSettings();
        if (store == nullptr)
        {
            return;
        }
        const ClipPreviewSettings* section = store->Find<ClipPreviewSettings>();
        if (section == nullptr)
        {
            return;
        }
        for (const ClipPreviewPref& p : section->prefs)
        {
            if (p.asset != InstanceId())
            {
                continue;
            }
            m_skeletonGuid = p.skeleton;
            if (!p.skeleton.IsNil() && m_context->Resources() != nullptr)
            {
                m_skeleton = m_context->Resources()->Bind<animation::Skeleton>(p.skeleton);
            }
            if (m_skeletonButton.Get() != nullptr)
            {
                m_skeletonButton->SetText(
                    ClipPickLabel(*m_context, u8"Skeleton: ", p.skeleton).AsView());
            }

            m_previewMeshId = p.mesh;
            if (!p.mesh.IsNil() && m_context->Resources() != nullptr)
            {
                m_previewMesh = m_context->Resources()->Bind<foundation::geometry::StaticMesh>(p.mesh);
            }
            // Point the preview MeshComponent at the restored mesh (bone matrices feed per frame).
            scene::Scene* scenePtr = m_preview ? m_preview->Scene() : nullptr;
            if (scenePtr != nullptr)
            {
                if (auto* meshes = scenePtr->GetSystem<engine::render::MeshComponentManager>())
                {
                    if (auto* mc = meshes->Get(m_meshEntity))
                    {
                        if (foundation::geometry::StaticMesh* pm = m_previewMesh.Get())
                        {
                            mc->mesh = pm;
                        }
                    }
                }
            }
            if (m_meshButton.Get() != nullptr)
            {
                m_meshButton->SetText(ClipPickLabel(*m_context, u8"Mesh: ", p.mesh).AsView());
            }
            return;
        }
    }

    void AnimationClipEditorPage::SavePreviewPref()
    {
        foundation::settings::Settings* store = m_context->ProjectEditorSettings();
        if (store == nullptr)
        {
            return;
        }
        ClipPreviewSettings& section = store->Section<ClipPreviewSettings>();
        for (ClipPreviewPref& p : section.prefs)
        {
            if (p.asset == InstanceId())
            {
                p.skeleton = m_skeletonGuid;
                p.mesh = m_previewMeshId;
                return;
            }
        }
        section.prefs.PushBack(ClipPreviewPref{InstanceId(), m_skeletonGuid, m_previewMeshId});
    }

    // ============================ Preview ===================================================

    void AnimationClipEditorPage::BuildPreviewScene()
    {
        scene::Scene* scenePtr = m_preview ? m_preview->Scene() : nullptr;
        if (scenePtr == nullptr)
        {
            return;
        }

        // A sun so a picked skinned mesh is lit (the skeleton wireframe needs none).
        const scene::EntityHandle sun = scenePtr->CreateEntity(u8"Sun");
        Transform st;
        st.rotation = Quaternion::FromAxisAngle(Float3{0, 1, 0}, 0.35f) *
                      Quaternion::FromAxisAngle(Float3{1, 0, 0}, -1.05f);
        scenePtr->SetLocalTransform(sun, st);
        if (auto* lights = scenePtr->GetSystem<engine::render::LightComponentManager>())
        {
            engine::render::LightComponent& light = lights->Add(sun);
            light.castsShadows = false;
        }

        // The optional skinned mesh: its MeshComponent gets bone matrices fed each frame from the
        // preview player (see UpdatePreview). No mesh bound until the user picks one.
        m_meshEntity = scenePtr->CreateEntity(u8"PreviewMesh");
        if (auto* meshes = scenePtr->GetSystem<engine::render::MeshComponentManager>())
        {
            meshes->Add(m_meshEntity);
        }
    }

    void AnimationClipEditorPage::PickPreviewSkeleton()
    {
        ui::UIContext* ctx = Ctx();
        if (ctx == nullptr || m_context->Project() == nullptr)
        {
            return;
        }
        AnimationClipEditorPage* self = this;
        Array<String> types;
        types.PushBack(String(u8"SkeletonAsset"));
        auto dialog = MakeRef<app::AssetPickerDialog>(foundation::core::DefaultAllocator(), *m_context, Move(types));
        dialog->OnPicked = [self](const Guid& picked)
        {
            self->m_skeletonGuid = picked;
            if (self->m_context->Resources() != nullptr && !picked.IsNil())
            {
                self->m_skeleton = self->m_context->Resources()->Bind<animation::Skeleton>(picked);
            }
            else
            {
                self->m_skeleton = foundation::resource::Proxy<animation::Skeleton>{};
            }
            String label(u8"Skeleton: ");
            if (self->m_context->Project() != nullptr && !picked.IsNil())
            {
                if (foundation::content::Instance* inst =
                        self->m_context->Project()->SourceDb().GetInstance(picked))
                {
                    label.Append(inst->Name());
                }
                else
                {
                    label.Append(u8"(missing)");
                }
            }
            else
            {
                label.Append(u8"(none)");
            }
            self->m_skeletonButton->SetText(label.AsView());
            self->SavePreviewPref();
        };
        dialog->Show(ctx);
    }

    void AnimationClipEditorPage::PickPreviewMesh()
    {
        ui::UIContext* ctx = Ctx();
        if (ctx == nullptr || m_context->Project() == nullptr)
        {
            return;
        }
        AnimationClipEditorPage* self = this;
        Array<String> types;
        types.PushBack(String(u8"SkinnedMeshAsset"));
        auto dialog = MakeRef<app::AssetPickerDialog>(foundation::core::DefaultAllocator(), *m_context, Move(types));
        dialog->OnPicked = [self](const Guid& picked)
        {
            self->m_previewMeshId = picked;
            if (self->m_context->Resources() != nullptr && !picked.IsNil())
            {
                self->m_previewMesh =
                    self->m_context->Resources()->Bind<foundation::geometry::StaticMesh>(picked);
            }
            else
            {
                self->m_previewMesh = foundation::resource::Proxy<foundation::geometry::StaticMesh>{};
            }
            // Point the preview MeshComponent at the picked mesh (bone matrices feed per frame).
            scene::Scene* scenePtr = self->m_preview ? self->m_preview->Scene() : nullptr;
            if (scenePtr != nullptr)
            {
                if (auto* meshes = scenePtr->GetSystem<engine::render::MeshComponentManager>())
                {
                    if (auto* mc = meshes->Get(self->m_meshEntity))
                    {
                        foundation::geometry::StaticMesh* pm = self->m_previewMesh.Get();
                        if (pm != nullptr)
                        {
                            mc->mesh = pm;
                        }
                        else
                        {
                            mc->mesh.SetDirect(RefPtr<foundation::geometry::StaticMesh>{});
                            mc->boneMatrices = nullptr;
                            mc->boneCount = 0;
                        }
                    }
                }
            }
            String label(u8"Mesh: ");
            if (self->m_context->Project() != nullptr && !picked.IsNil())
            {
                if (foundation::content::Instance* inst =
                        self->m_context->Project()->SourceDb().GetInstance(picked))
                {
                    label.Append(inst->Name());
                }
                else
                {
                    label.Append(u8"(missing)");
                }
            }
            else
            {
                label.Append(u8"(none)");
            }
            self->m_meshButton->SetText(label.AsView());
            self->SavePreviewPref();
        };
        dialog->Show(ctx);
    }

    void AnimationClipEditorPage::UpdatePreview(f32 dt)
    {
        animation::AnimationClip* clip = m_clip.Get();
        animation::Skeleton* skeleton = m_skeleton.Get();
        if (clip == nullptr || clip->duration <= 0.0f)
        {
            if (m_timeLabel.Get() != nullptr)
            {
                m_timeLabel->SetText(u8"(clip not cooked)");
            }
            return;
        }

        if (m_playing)
        {
            m_time += dt;
            if (m_time > clip->duration)
            {
                m_time = clip->isLooping ? std::fmod(m_time, clip->duration) : clip->duration;
                if (!clip->isLooping)
                {
                    m_playing = false;
                    m_playButton->SetText(u8"Play");
                }
            }
            // Echo playback into the slider without re-entering the scrub handler.
            m_scrubbing = true;
            m_timeSlider->Value.SetValue(m_time / clip->duration);
            m_scrubbing = false;
        }
        if (m_timeLabel.Get() != nullptr)
        {
            m_timeLabel->SetText(Format(u8"{}s / {}s", static_cast<i32>(m_time * 100.0f) / 100.0f,
                                        static_cast<i32>(clip->duration * 100.0f) / 100.0f)
                                     .AsView());
        }

        scene::Scene* scenePtr = m_preview ? m_preview->Scene() : nullptr;
        if (skeleton == nullptr || skeleton->BoneCount() <= 0 || m_preview.Get() == nullptr ||
            !m_preview->IsValid() || scenePtr == nullptr)
        {
            return;
        }
        const usize boneCount = static_cast<usize>(skeleton->BoneCount());
        m_poseScratch.Resize(boneCount);
        animation::SampleClip(*clip, *skeleton, m_time,
                              Span<animation::BoneTransform>{m_poseScratch.Data(), boneCount});

        // Skinned preview mesh: drive the player to the SAME m_time and push its skinning matrices
        // onto the MeshComponent (borrowed for this frame's render).
        if (m_previewMesh.Get() != nullptr)
        {
            if (m_previewPlayer.Get() == nullptr || m_playerSkeleton != skeleton)
            {
                m_previewPlayer =
                    MakeUnique<animation::AnimationPlayer>(foundation::core::DefaultAllocator(), *skeleton);
                m_playerSkeleton = skeleton;
                m_playerClip = nullptr;
            }
            if (m_playerClip != clip)
            {
                m_playerClip = clip;
                m_previewPlayer->Play(clip);
            }
            m_previewPlayer->SetCurrentTime(m_time);
            m_previewPlayer->Update(0.0f); // resample at m_time without advancing
            const Span<const Float4x4> mats = m_previewPlayer->GetSkinningMatrices();
            if (auto* meshes = scenePtr->GetSystem<engine::render::MeshComponentManager>())
            {
                if (auto* mc = meshes->Get(m_meshEntity))
                {
                    mc->boneMatrices = mats.Data();
                    mc->boneCount = static_cast<u32>(mats.Size());
                }
            }
        }

        auto& draw = m_preview->SceneDebugDraw();
        draw.DrawGrid(Float3{0.0f, 0.0f, 0.0f}, 4.0f, 8, Color{0.25f, 0.25f, 0.28f, 1.0f});
        DrawSkeletonWireframe(draw, *skeleton,
                              Span<const animation::BoneTransform>{m_poseScratch.Data(), boneCount},
                              m_worldScratch);
    }

    // ============================ Inspector =================================================

    void AnimationClipEditorPage::RebuildGrid()
    {
        m_grid->Clear();
        if (m_asset.Get() == nullptr)
        {
            return;
        }
        AnimationClipEditorPage* self = this;
        animation::AnimationClipSource& source = m_asset->source;
        ui::toolkit::PropertyGrid& g = *m_grid;

        // --- stats (read-only) ---
        {
            const StringView cat = u8"Clip";
            auto stat = [&](StringView name, String value)
            {
                g.AddProperty(RefPtr<ui::toolkit::PropertyEditor>(
                    MakeRef<ui::toolkit::StringEditor>(foundation::core::DefaultAllocator(), name, value.AsView(),
                                                       Function<void(StringView)>{}, cat)
                        .Get()));
            };
            stat(u8"Name", String(source.name.AsView()));
            stat(u8"Duration", Format(u8"{} s", source.duration));
            stat(u8"Tracks", Format(u8"{}", source.trackBone.Size()));

            g.AddProperty(RefPtr<ui::toolkit::PropertyEditor>(
                MakeRef<ui::toolkit::BoolEditor>(foundation::core::DefaultAllocator(), u8"Looping", source.isLooping,
                                                 Function<void(bool)>{[self, &source](bool v)
                                                                      {
                                                                          source.isLooping = v;
                                                                          self->CommitEdit(
                                                                              u8"clip-loop");
                                                                      }},
                                                 cat)
                    .Get()));
        }

        // --- events (time + name, add/remove, undoable) ---
        while (source.eventNames.Size() < source.eventTimes.Size())
        {
            source.eventNames.PushBack(String{});
        }
        while (source.eventTimes.Size() < source.eventNames.Size())
        {
            source.eventTimes.PushBack(0.0f);
        }
        for (usize e = 0; e < source.eventTimes.Size(); ++e)
        {
            const String cat = Format(u8"Event {}", e);
            g.AddProperty(RefPtr<ui::toolkit::PropertyEditor>(
                MakeRef<ui::toolkit::FloatEditor>(
                    foundation::core::DefaultAllocator(), u8"Time (s)", static_cast<f64>(source.eventTimes[e]), 0.0,
                    static_cast<f64>(Max(source.duration, 0.0f)), 0.01, 3,
                    Function<void(f64)>{[self, &source, e](f64 v)
                                        {
                                            source.eventTimes[e] = static_cast<f32>(v);
                                            self->CommitEdit(u8"event-time");
                                        }},
                    cat.AsView())
                    .Get()));
            g.AddProperty(RefPtr<ui::toolkit::PropertyEditor>(
                MakeRef<ui::toolkit::StringEditor>(
                    foundation::core::DefaultAllocator(), u8"Name", source.eventNames[e].AsView(),
                    Function<void(StringView)>{[self, &source, e](StringView v)
                                               {
                                                   source.eventNames[e] = String(v);
                                                   self->CommitEdit(u8"event-name");
                                               }},
                    cat.AsView())
                    .Get()));
            const usize eventIdx = e;
            g.AddProperty(RefPtr<ui::toolkit::PropertyEditor>(
                MakeRef<ui::toolkit::ButtonEditor>(
                    foundation::core::DefaultAllocator(), u8"Remove Event",
                    Function<void()>{[self, eventIdx]()
                                     {
                                         self->QueueStructural(
                                             u8"del-event",
                                             Function<void()>{
                                                 [self, eventIdx]()
                                                 {
                                                     animation::AnimationClipSource& s =
                                                         self->m_asset->source;
                                                     if (eventIdx < s.eventTimes.Size())
                                                     {
                                                         s.eventTimes.RemoveAt(eventIdx);
                                                     }
                                                     if (eventIdx < s.eventNames.Size())
                                                     {
                                                         s.eventNames.RemoveAt(eventIdx);
                                                     }
                                                 }});
                                     }},
                    cat.AsView())
                    .Get()));
        }
        g.AddProperty(RefPtr<ui::toolkit::PropertyEditor>(
            MakeRef<ui::toolkit::ButtonEditor>(
                foundation::core::DefaultAllocator(), u8"+ Add Event",
                Function<void()>{[self]()
                                 {
                                     self->QueueStructural(
                                         u8"add-event",
                                         Function<void()>{[self]()
                                                          {
                                                              animation::AnimationClipSource& s =
                                                                  self->m_asset->source;
                                                              s.eventTimes.PushBack(
                                                                  self->m_time); // at the playhead
                                                              s.eventNames.PushBack(
                                                                  String(u8"event"));
                                                          }});
                                 }},
                StringView(u8"Events"))
                .Get()));
    }

    void AnimationClipEditorPage::QueueStructural(StringView undoKey, Function<void()> mutate)
    {
        AnimationClipEditorPage* self = this;
        String key(undoKey);
        auto run = [self, mutate = Move(mutate), key = Move(key)]() mutable
        {
            mutate();
            Array<byte> after = self->SnapshotAsset();
            (void)self->Commands().Execute(
                UniquePtr<IEditorCommand>(foundation::core::DefaultAllocator().New<EditClipCommand>(
                                              *self, key.AsView(), self->m_undoBaseline, after),
                                          foundation::core::DefaultAllocator()));
            self->m_undoBaseline = Move(after);
            self->RebuildGrid();
            self->MarkDirty();
        };
        if (ui::UIContext* ctx = Ctx())
        {
            ctx->MutationQueueRef().QueueAction(Function<void()>{Move(run)});
        }
        else
        {
            run();
        }
    }

    // ============================ Undo ======================================================

    Array<byte> AnimationClipEditorPage::SnapshotAsset() const
    {
        Array<byte> blob;
        if (m_asset.Get() == nullptr)
        {
            return blob;
        }
        MemoryStream stream;
        BinarySerializer ar(stream, SerializeMode::Write);
        m_asset->Serialize(ar);
        const Span<const byte> bytes = stream.Bytes();
        blob.Reserve(bytes.Size());
        for (byte b : bytes)
        {
            blob.PushBack(b);
        }
        return blob;
    }

    void AnimationClipEditorPage::ApplyAssetBlob(const Array<byte>& blob)
    {
        if (m_asset.Get() == nullptr)
        {
            return;
        }
        Array<byte> current = SnapshotAsset();
        if (current.Size() == blob.Size())
        {
            bool same = true;
            for (usize i = 0; i < blob.Size(); ++i)
            {
                if (current[i] != blob[i])
                {
                    same = false;
                    break;
                }
            }
            if (same)
            {
                return;
            }
        }
        MemoryStream stream;
        (void)stream.Write(blob.Data(), blob.Size());
        (void)stream.Seek(0, SeekOrigin::Begin);
        BinarySerializer ar(stream, SerializeMode::Read);
        // Clear the array fields in place (the source is an ISerializable - no copy assign).
        animation::AnimationClipSource& source = m_asset->source;
        source.trackBone.Clear();
        source.trackKind.Clear();
        source.trackInterp.Clear();
        source.trackStart.Clear();
        source.trackCount.Clear();
        source.eventTimes.Clear();
        source.eventNames.Clear();
        m_asset->Serialize(ar);
        m_undoBaseline = blob;
        AnimationClipEditorPage* self = this;
        if (ui::UIContext* ctx = Ctx())
        {
            ctx->MutationQueueRef().QueueAction(
                Function<void()>{[self]() { self->RebuildGrid(); }});
        }
        else
        {
            RebuildGrid();
        }
        MarkDirty();
    }

    void AnimationClipEditorPage::CommitEdit(StringView mergeKey)
    {
        Array<byte> after = SnapshotAsset();
        (void)Commands().Execute(UniquePtr<IEditorCommand>(
            foundation::core::DefaultAllocator().New<EditClipCommand>(*this, mergeKey, m_undoBaseline, after),
            foundation::core::DefaultAllocator()));
        m_undoBaseline = Move(after);
        MarkDirty();
    }

    // ============================ Frame / save / close ======================================

    ui::UIContext* AnimationClipEditorPage::Ctx() const
    {
        return (m_grid.Get() != nullptr) ? m_grid->Context : nullptr;
    }

    void AnimationClipEditorPage::OnUpdate(runtime::IApplicationHost&, f32 dt)
    {
        UpdatePreview(dt);
        if (m_preview)
        {
            m_preview->Update(dt);
        }
    }

    void AnimationClipEditorPage::OnRenderWindow(runtime::IApplicationHost&,
                                                 foundation::graphics::FrameContext& frame)
    {
        if (m_preview)
        {
            m_preview->RenderFrame(frame);
        }
    }

    Status AnimationClipEditorPage::Save()
    {
        if (m_asset.Get() == nullptr || m_context->Project() == nullptr)
        {
            return Status{ErrorCode::NotFound};
        }
        foundation::content::Instance* instance =
            m_context->Project()->SourceDb().GetInstance(InstanceId());
        if (instance == nullptr)
        {
            return Status{ErrorCode::NotFound};
        }
        const Status saved = instance->WriteObject(*m_asset);
        if (saved.IsOk())
        {
            ClearDirty();
            m_context->RequestCook(false);
            LOG_INFO(u8"Editor", u8"saved animation clip '{}'", m_title);
        }
        return saved;
    }

    void AnimationClipEditorPage::OnClose()
    {
        if (m_preview)
        {
            m_preview->Shutdown();
        }
    }

    // ============================ Factory ===================================================

    const TypeInfo* AnimationClipPageFactory::PrimaryType() const
    {
        return &pipeline::AnimationClipAsset::StaticType();
    }

    UniquePtr<EditorPage>
    AnimationClipPageFactory::CreatePage(EditorContext& context,
                                         foundation::content::Instance& instance)
    {
        auto* page =
            foundation::core::DefaultAllocator().New<AnimationClipEditorPage>(context, *m_host, *m_uiHost, instance);
        return UniquePtr<EditorPage>(page, foundation::core::DefaultAllocator());
    }

    void RegisterAnimationClipEditor(EditorContext& context, runtime::IApplicationHost& host,
                                     ui::runtime::UIHost& uiHost)
    {
        // The preview-prefs section (registered before the app loads the per-project store).
        GlobalTypeRegistry().Register(ClipPreviewSettings::StaticType(), TypeDomain(u8"Editor"));
        RegisterSerializable<ClipPreviewSettings>();

        context.Pages().Register(UniquePtr<IEditorPageFactory>(
            foundation::core::DefaultAllocator().New<AnimationClipPageFactory>(host, uiHost), foundation::core::DefaultAllocator()));
    }

    RTTI_DEFINE_OBJECT_VERSIONED(ClipPreviewSettings, "rtti::editor::editor.clip", 1)
}
