// Editor::PropertyAnimation - the clip page implementation (UI construction + Save). Kept out of the
// interface (heavy UI bodies; the module-hygiene rule).

module;
#include "Core/Prelude.h"
#include "Core/Log/Log.h"
#include <cstdlib>

module editor.propertyanimation;

import foundation.core;
import foundation.content;
import foundation.ui;
import foundation.ui.toolkit;
import editor.core;
import editor.app;

using namespace foundation::core;

namespace editor
{
    namespace
    {
        constexpr propanim::TrackValueKind kKinds[] = {
            propanim::TrackValueKind::Float, propanim::TrackValueKind::Float3,
            propanim::TrackValueKind::Color, propanim::TrackValueKind::Quat};

        // The per-channel labels for a kind (x/y/z, r/g/b/a); empty (Quat) uses its own row.
        StringView ChannelLabel(propanim::TrackValueKind kind, u32 channel)
        {
            if (kind == propanim::TrackValueKind::Color)
            {
                const StringView rgba[] = {u8"r", u8"g", u8"b", u8"a"};
                return rgba[channel < 4 ? channel : 0];
            }
            const StringView xyz[] = {u8"x", u8"y", u8"z"};
            return xyz[channel < 3 ? channel : 0];
        }
    }

    StringView PropertyAnimationClipEditorPage::KindName(propanim::TrackValueKind kind)
    {
        switch (kind)
        {
        case propanim::TrackValueKind::Float:
            return u8"Float";
        case propanim::TrackValueKind::Float3:
            return u8"Float3";
        case propanim::TrackValueKind::Color:
            return u8"Color";
        case propanim::TrackValueKind::Quat:
            return u8"Quat";
        }
        return u8"?";
    }

    StringView PropertyAnimationClipEditorPage::InterpName(CurveKeyInterpolation interp)
    {
        switch (interp)
        {
        case CurveKeyInterpolation::Constant:
            return u8"Step";
        case CurveKeyInterpolation::Linear:
            return u8"Linear";
        case CurveKeyInterpolation::Cubic:
            return u8"Cubic";
        }
        return u8"?";
    }

    ui::toolkit::CurveInterpolation
    PropertyAnimationClipEditorPage::ClipToCanvasInterp(CurveKeyInterpolation i)
    {
        switch (i)
        {
        case CurveKeyInterpolation::Constant:
            return ui::toolkit::CurveInterpolation::Step;
        case CurveKeyInterpolation::Linear:
            return ui::toolkit::CurveInterpolation::Linear;
        case CurveKeyInterpolation::Cubic:
            return ui::toolkit::CurveInterpolation::Hermite;
        }
        return ui::toolkit::CurveInterpolation::Linear;
    }

    CurveKeyInterpolation
    PropertyAnimationClipEditorPage::CanvasToClipInterp(ui::toolkit::CurveInterpolation i)
    {
        switch (i)
        {
        case ui::toolkit::CurveInterpolation::Step:
            return CurveKeyInterpolation::Constant;
        case ui::toolkit::CurveInterpolation::Linear:
            return CurveKeyInterpolation::Linear;
        case ui::toolkit::CurveInterpolation::Hermite:
            return CurveKeyInterpolation::Cubic;
        }
        return CurveKeyInterpolation::Linear;
    }

    PropertyAnimationClipEditorPage::PropertyAnimationClipEditorPage(
        EditorContext& context, runtime::IApplicationHost&, foundation::content::Instance& instance)
        : m_context(&context), m_title(instance.Name())
    {
        RefPtr<ISerializable> object = instance.ReadObject();
        if (auto* asset = Cast<pipeline::PropertyAnimationClipAsset>(object.Get()))
        {
            m_fileName = asset->fileName;      // preserved verbatim on save
            asset->source.FillClip(m_clip);    // flatten the cooked wire into the editing model
        }
        m_editDuration = Max(m_clip.ComputeDuration(), 1.0f); // canvas time-axis scale

        auto column = MakeRef<ui::FlexLayout>(DefaultAllocator());
        column->Direction = ui::Orientation::Vertical;
        column->Padding = ui::Thickness{8, 6};

        m_toolbar = MakeRef<app::PageToolbar>(DefaultAllocator(), *this);
        {
            auto lp = MakeRef<ui::FlexLayoutParams>(DefaultAllocator());
            lp->Width = ui::SizeSpec::Match();
            lp->Height = ui::SizeSpec::Fixed(ui::Unit::Px(28));
            column->AddView(m_toolbar.Get(), lp);
        }

        m_scroll = MakeRef<ui::ScrollView>(DefaultAllocator());
        m_rows = MakeRef<ui::FlexLayout>(DefaultAllocator());
        m_rows->Direction = ui::Orientation::Vertical;
        m_rows->Spacing = 2.0f;
        m_scroll->AddView(m_rows.Get());
        {
            auto lp = MakeRef<ui::FlexLayoutParams>(DefaultAllocator());
            lp->Width = ui::SizeSpec::Match();
            lp->Grow = 1.0f;
            column->AddView(m_scroll.Get(), lp);
        }
        m_content = column;
        Rebuild();
    }

    RefPtr<ui::FlexLayout> PropertyAnimationClipEditorPage::MakeRow(f32 indent, f32 height)
    {
        auto row = MakeRef<ui::FlexLayout>(DefaultAllocator());
        row->Direction = ui::Orientation::Horizontal;
        row->Spacing = 4.0f;
        row->Padding = ui::Thickness{indent, 0};
        auto lp = MakeRef<ui::FlexLayoutParams>(DefaultAllocator());
        lp->Width = ui::SizeSpec::Match();
        lp->Height = ui::SizeSpec::Fixed(ui::Unit::Px(height));
        m_rows->AddView(row.Get(), lp);
        return row;
    }

    ui::Button* PropertyAnimationClipEditorPage::MakeButton(ui::FlexLayout& row, StringView label,
                                                            f32 width, Function<void()> onClick)
    {
        auto button = MakeRef<ui::Button>(DefaultAllocator(), label);
        button->FontSize.SetValue(Optional<f32>{11.0f});
        button->OnClick.Add(
            [fn = Move(onClick)](ui::ButtonBase*)
            {
                if (fn)
                {
                    fn();
                }
            });
        auto lp = MakeRef<ui::FlexLayoutParams>(DefaultAllocator());
        lp->Width = ui::SizeSpec::Fixed(ui::Unit::Px(width));
        lp->Height = ui::SizeSpec::Match();
        row.AddView(button.Get(), lp);
        return button.Get();
    }

    void PropertyAnimationClipEditorPage::AddLabel(ui::FlexLayout& row, StringView text, f32 grow,
                                                   f32 width)
    {
        auto label = MakeRef<ui::Label>(DefaultAllocator(), text);
        label->FontSize.SetValue(12.0f);
        auto lp = MakeRef<ui::FlexLayoutParams>(DefaultAllocator());
        if (grow > 0.0f)
        {
            lp->Grow = grow;
        }
        else if (width > 0.0f)
        {
            lp->Width = ui::SizeSpec::Fixed(ui::Unit::Px(width));
        }
        lp->Height = ui::SizeSpec::Match();
        row.AddView(label.Get(), lp);
    }

    void PropertyAnimationClipEditorPage::AddTextField(ui::FlexLayout& row, StringView value,
                                                       Function<void(StringView)> commit, f32 width)
    {
        auto field = MakeRef<ui::EditableLabel>(DefaultAllocator());
        field->SetText(value);
        field->FontSize.SetValue(12.0f);
        field->OnRenameCommitted.Add(
            [fn = Move(commit)](ui::EditableLabel*, StringView committed)
            {
                if (fn && !committed.IsEmpty())
                {
                    fn(committed);
                }
            });
        auto lp = MakeRef<ui::FlexLayoutParams>(DefaultAllocator());
        lp->Width = ui::SizeSpec::Fixed(ui::Unit::Px(width));
        lp->Height = ui::SizeSpec::Match();
        row.AddView(field.Get(), lp);
    }

    void PropertyAnimationClipEditorPage::AddFloatField(ui::FlexLayout& row, f32 value,
                                                        Function<void(f32)> commit, f32 width)
    {
        auto field = MakeRef<ui::EditableLabel>(DefaultAllocator());
        String text;
        AppendValue(text, value);
        field->SetText(text.AsView());
        field->FontSize.SetValue(12.0f);
        field->OnRenameCommitted.Add(
            [fn = Move(commit)](ui::EditableLabel*, StringView committed)
            {
                if (!fn || committed.IsEmpty())
                {
                    return;
                }
                String buffer(committed);
                char* end = nullptr;
                const f32 parsed = std::strtof(reinterpret_cast<const char*>(buffer.CStr()), &end);
                if (end != reinterpret_cast<const char*>(buffer.CStr()))
                {
                    fn(parsed);
                }
            });
        auto lp = MakeRef<ui::FlexLayoutParams>(DefaultAllocator());
        lp->Width = ui::SizeSpec::Fixed(ui::Unit::Px(width));
        lp->Height = ui::SizeSpec::Match();
        row.AddView(field.Get(), lp);
    }

    void PropertyAnimationClipEditorPage::BuildTransportRow()
    {
        auto row = MakeRow(0.0f, 26.0f);
        AddLabel(*row, u8"Scrub", 0.0f, 42.0f);
        PropertyAnimationClipEditorPage* self = this;
        AddFloatField(*row, m_scrubTime,
                      [self](f32 t)
                      {
                          self->m_scrubTime = t;
                          self->RefreshPreview();
                      },
                      64.0f);
        AddLabel(*row, u8"Length", 0.0f, 52.0f);
        AddFloatField(*row, m_editDuration,
                      [self](f32 d)
                      {
                          self->m_editDuration = (d > 1e-3f) ? d : 1.0f;
                          self->RequestRebuild(); // the canvas time axis rescales
                      },
                      56.0f);

        m_preview = MakeRef<ui::Label>(DefaultAllocator(), StringView(u8""));
        m_preview->FontSize.SetValue(11.0f);
        {
            auto lp = MakeRef<ui::FlexLayoutParams>(DefaultAllocator());
            lp->Grow = 1.0f;
            lp->Height = ui::SizeSpec::Match();
            row->AddView(m_preview.Get(), lp);
        }
        RefreshPreview();
    }

    void PropertyAnimationClipEditorPage::RefreshPreview()
    {
        if (m_preview.Get() == nullptr)
        {
            return;
        }
        // Sample every track at the scrub time; show "component.path=..." for each (the P1 preview -
        // driving a selected scene entity is a follow-up needing the cross-page selection seam).
        String out;
        for (usize i = 0; i < m_clip.tracks.Size(); ++i)
        {
            const propanim::PropertyTrack& t = m_clip.tracks[i];
            if (i > 0)
            {
                out += u8"   ";
            }
            out += t.propertyPath.AsView();
            out += u8"=";
            const Variant v = t.Sample(m_scrubTime);
            switch (t.kind)
            {
            case propanim::TrackValueKind::Float:
                AppendValue(out, v.Get<f32>());
                break;
            case propanim::TrackValueKind::Float3:
            {
                const Float3 f = v.Get<Float3>();
                out += u8"(";
                AppendValue(out, f.x);
                out += u8",";
                AppendValue(out, f.y);
                out += u8",";
                AppendValue(out, f.z);
                out += u8")";
                break;
            }
            case propanim::TrackValueKind::Color:
            {
                const Color c = v.Get<Color>();
                out += u8"(";
                AppendValue(out, c.r);
                out += u8",";
                AppendValue(out, c.g);
                out += u8",";
                AppendValue(out, c.b);
                out += u8",";
                AppendValue(out, c.a);
                out += u8")";
                break;
            }
            case propanim::TrackValueKind::Quat:
                out += u8"quat";
                break;
            }
        }
        m_preview->SetText(out.AsView());
    }

    void PropertyAnimationClipEditorPage::BuildTrackRows(usize trackIndex)
    {
        PropertyAnimationClipEditorPage* self = this;
        const propanim::PropertyTrack& track = m_clip.tracks[trackIndex];

        // --- track header: component + property + kind + remove ---
        auto header = MakeRow(0.0f, 26.0f);
        AddTextField(*header, track.componentType.AsView(),
                     [self, trackIndex](StringView v)
                     { self->Mutate([&](propanim::PropertyAnimationClip& c)
                                    { c.tracks[trackIndex].componentType = String(v); }); },
                     120.0f);
        AddLabel(*header, u8".", 0.0f, 8.0f);
        AddTextField(*header, track.propertyPath.AsView(),
                     [self, trackIndex](StringView v)
                     { self->Mutate([&](propanim::PropertyAnimationClip& c)
                                    { c.tracks[trackIndex].propertyPath = String(v); }); },
                     120.0f);
        MakeButton(*header, KindName(track.kind), 58.0f,
                   [self, trackIndex]()
                   {
                       self->Mutate(
                           [&](propanim::PropertyAnimationClip& c)
                           {
                               propanim::PropertyTrack& t = c.tracks[trackIndex];
                               usize k = 0;
                               for (usize j = 0; j < 4; ++j)
                               {
                                   if (kKinds[j] == t.kind)
                                   {
                                       k = j;
                                       break;
                                   }
                               }
                               t.kind = kKinds[(k + 1) % 4];
                           });
                   });
        MakeButton(*header, u8"x", 22.0f,
                   [self, trackIndex]()
                   { self->Mutate([&](propanim::PropertyAnimationClip& c)
                                  { c.tracks.RemoveAt(trackIndex); }); });

        // --- keyframe rows ---
        if (track.kind == propanim::TrackValueKind::Quat)
        {
            for (usize ki = 0; ki < track.quatKeys.Size(); ++ki)
            {
                const propanim::QuatKey& qk = track.quatKeys[ki];
                auto krow = MakeRow(18.0f);
                AddFloatField(*krow, qk.time,
                              [self, trackIndex, ki](f32 t)
                              { self->Mutate([&](propanim::PropertyAnimationClip& c)
                                             { c.tracks[trackIndex].quatKeys[ki].time = t; }); },
                              50.0f);
                const f32 comps[4] = {qk.value.x, qk.value.y, qk.value.z, qk.value.w};
                for (u32 ci = 0; ci < 4; ++ci)
                {
                    AddFloatField(*krow, comps[ci],
                                  [self, trackIndex, ki, ci](f32 v)
                                  {
                                      self->Mutate(
                                          [&](propanim::PropertyAnimationClip& c)
                                          {
                                              Quaternion& q = c.tracks[trackIndex].quatKeys[ki].value;
                                              (ci == 0 ? q.x : ci == 1 ? q.y : ci == 2 ? q.z : q.w) = v;
                                          });
                                  },
                                  48.0f);
                }
                MakeButton(*krow, u8"x", 22.0f,
                           [self, trackIndex, ki]()
                           { self->Mutate([&](propanim::PropertyAnimationClip& c)
                                          { c.tracks[trackIndex].quatKeys.RemoveAt(ki); }); });
            }
            MakeButton(*MakeRow(18.0f), u8"+ Key", 60.0f,
                       [self, trackIndex]()
                       {
                           self->Mutate(
                               [&](propanim::PropertyAnimationClip& c)
                               {
                                   propanim::PropertyTrack& t = c.tracks[trackIndex];
                                   const f32 at = t.quatKeys.IsEmpty()
                                                      ? 0.0f
                                                      : t.quatKeys[t.quatKeys.Size() - 1].time + 1.0f;
                                   t.quatKeys.PushBack(propanim::QuatKey{at, Quaternion::Identity});
                               });
                       });
        }
        else
        {
            // Scalar-channel tracks (Float/Float3/Color): an interactive CurveCanvas (times shown
            // normalized by the clip length; add via click, drag to edit, right-click to delete).
            AddCurveCanvas(trackIndex);
        }
    }

    void PropertyAnimationClipEditorPage::AddCurveCanvas(usize trackIndex)
    {
        PropertyAnimationClipEditorPage* self = this;
        const propanim::PropertyTrack& track = m_clip.tracks[trackIndex];
        const u32 channels = propanim::ChannelCount(track.kind);
        if (channels == 0)
        {
            return;
        }

        // Per-track interpolation cycle (the canvas interpolates PER CHANNEL, not per key - a P1
        // simplification): applies to every channel of this track.
        {
            auto irow = MakeRow(18.0f, 22.0f);
            const CurveKeyInterpolation cur =
                (track.channels[0].KeyCount() > 0) ? track.channels[0].Keys()[0].interpolation
                                                   : CurveKeyInterpolation::Linear;
            MakeButton(*irow, InterpName(cur), 64.0f,
                       [self, trackIndex]()
                       {
                           self->Mutate(
                               [&](propanim::PropertyAnimationClip& c)
                               {
                                   propanim::PropertyTrack& tr = c.tracks[trackIndex];
                                   const u32 chn = propanim::ChannelCount(tr.kind);
                                   CurveKeyInterpolation next = CurveKeyInterpolation::Linear;
                                   if (chn > 0 && tr.channels[0].KeyCount() > 0)
                                   {
                                       const CurveKeyInterpolation prev =
                                           tr.channels[0].Keys()[0].interpolation;
                                       next = (prev == CurveKeyInterpolation::Constant)
                                                  ? CurveKeyInterpolation::Linear
                                              : (prev == CurveKeyInterpolation::Linear)
                                                  ? CurveKeyInterpolation::Cubic
                                                  : CurveKeyInterpolation::Constant;
                                   }
                                   for (u32 ch = 0; ch < chn; ++ch)
                                   {
                                       for (usize k = 0; k < tr.channels[ch].KeyCount(); ++k)
                                       {
                                           tr.channels[ch].Keys()[k].interpolation = next;
                                       }
                                   }
                               });
                       });
            AddLabel(*irow, u8"(click canvas to add a key; drag to edit; right-click to delete)", 1.0f);
        }

        auto canvas = MakeRef<ui::toolkit::CurveCanvas>(DefaultAllocator());
        canvas->MaxKeys = 64;
        canvas->AutoFitValueRange = true;
        canvas->LinkedTime = channels > 1;

        const CurveKeyInterpolation trackInterp =
            (track.channels[0].KeyCount() > 0) ? track.channels[0].Keys()[0].interpolation
                                               : CurveKeyInterpolation::Linear;
        const Color stroke[4] = {Color{0.9f, 0.4f, 0.4f, 1.0f}, Color{0.4f, 0.9f, 0.5f, 1.0f},
                                 Color{0.45f, 0.65f, 1.0f, 1.0f}, Color{0.85f, 0.85f, 0.4f, 1.0f}};
        Array<ui::toolkit::ChannelDescriptor> descs;
        for (u32 ch = 0; ch < channels; ++ch)
        {
            ui::toolkit::ChannelDescriptor d;
            d.Name = String(ChannelLabel(track.kind, ch));
            d.StrokeColor = stroke[ch < 4 ? ch : 0];
            d.Interpolation = ClipToCanvasInterp(trackInterp);
            descs.PushBack(Move(d));
        }
        canvas->SetChannels(Span<const ui::toolkit::ChannelDescriptor>{descs.Data(), descs.Size()});
        PushTrackToCanvas(trackIndex, *canvas);

        ui::toolkit::CurveCanvas* raw = canvas.Get();
        canvas->OnEditBegin.Add([self]() { self->m_gestureBefore = self->m_clip; });
        canvas->OnKeyChanged.Add(
            [self, trackIndex, raw](i32, i32) { self->WriteBackTrack(trackIndex, *raw); });
        canvas->OnKeyAdded.Add(
            [self, trackIndex, raw](i32, i32) { self->WriteBackTrack(trackIndex, *raw); });
        canvas->OnKeyRemoved.Add(
            [self, trackIndex, raw](i32, i32) { self->WriteBackTrack(trackIndex, *raw); });
        canvas->OnEditEnd.Add(
            [self]()
            {
                // One undo step per gesture: the live edits already mutated m_clip; record before/after.
                propanim::PropertyAnimationClip after = self->m_clip;
                after.duration = after.ComputeDuration();
                (void)self->Commands().Execute(UniquePtr<IEditorCommand>(
                    DefaultAllocator().New<ClipEditCommand>(*self, Move(self->m_gestureBefore),
                                                            Move(after)),
                    DefaultAllocator()));
            });

        auto wrap = MakeRef<ui::FlexLayout>(DefaultAllocator());
        wrap->Direction = ui::Orientation::Vertical;
        wrap->Padding = ui::Thickness{18, 0};
        auto lp = MakeRef<ui::FlexLayoutParams>(DefaultAllocator());
        lp->Width = ui::SizeSpec::Match();
        lp->Height = ui::SizeSpec::Fixed(ui::Unit::Px(120.0f));
        wrap->AddView(canvas.Get(), lp);
        auto wlp = MakeRef<ui::FlexLayoutParams>(DefaultAllocator());
        wlp->Width = ui::SizeSpec::Match();
        wlp->Height = ui::SizeSpec::Fixed(ui::Unit::Px(122.0f));
        m_rows->AddView(wrap.Get(), wlp);
    }

    void PropertyAnimationClipEditorPage::PushTrackToCanvas(usize trackIndex,
                                                           ui::toolkit::CurveCanvas& canvas)
    {
        const propanim::PropertyTrack& track = m_clip.tracks[trackIndex];
        const u32 channels = propanim::ChannelCount(track.kind);
        const f32 dur = (m_editDuration > 1e-3f) ? m_editDuration : 1.0f;
        for (u32 ch = 0; ch < channels; ++ch)
        {
            Array<ui::toolkit::CurveCanvas::Key> keys;
            const Curve& cur = track.channels[ch];
            for (usize i = 0; i < cur.KeyCount(); ++i)
            {
                const CurveKey& k = cur.Keys()[i];
                // Normalize time to [0,1]; tangents are slope dy/dt, so rescale by the duration.
                keys.PushBack(ui::toolkit::CurveCanvas::Key{k.time / dur, k.value,
                                                            k.tangentIn * dur, k.tangentOut * dur});
            }
            canvas.SetKeys(static_cast<i32>(ch),
                           Span<const ui::toolkit::CurveCanvas::Key>{keys.Data(), keys.Size()});
        }
    }

    void PropertyAnimationClipEditorPage::WriteBackTrack(usize trackIndex,
                                                        ui::toolkit::CurveCanvas& canvas)
    {
        if (trackIndex >= m_clip.tracks.Size())
        {
            return;
        }
        propanim::PropertyTrack& track = m_clip.tracks[trackIndex];
        const u32 channels = propanim::ChannelCount(track.kind);
        const f32 dur = (m_editDuration > 1e-3f) ? m_editDuration : 1.0f;
        for (u32 ch = 0; ch < channels; ++ch)
        {
            const CurveKeyInterpolation interp =
                CanvasToClipInterp(canvas.GetChannelDescriptor(static_cast<i32>(ch)).Interpolation);
            const i32 n = canvas.GetKeyCount(static_cast<i32>(ch));
            track.channels[ch].Clear();
            for (i32 i = 0; i < n; ++i)
            {
                const ui::toolkit::CurveCanvas::Key k = canvas.GetKey(static_cast<i32>(ch), i);
                CurveKey ck;
                ck.time = k.Time * dur;
                ck.value = k.Value;
                ck.tangentIn = k.TangentIn / dur;
                ck.tangentOut = k.TangentOut / dur;
                ck.interpolation = interp;
                track.channels[ch].AddKey(ck);
            }
        }
        m_clip.duration = m_clip.ComputeDuration();
        MarkDirty();
        RefreshPreview();
    }

    void PropertyAnimationClipEditorPage::Rebuild()
    {
        m_rows->RemoveAllViews();
        BuildTransportRow();
        for (usize i = 0; i < m_clip.tracks.Size(); ++i)
        {
            BuildTrackRows(i);
        }
        PropertyAnimationClipEditorPage* self = this;
        MakeButton(*MakeRow(0.0f, 26.0f), u8"+ Track", 80.0f,
                   [self]()
                   {
                       self->Mutate(
                           [&](propanim::PropertyAnimationClip& c)
                           {
                               propanim::PropertyTrack t;
                               t.componentType = String(u8"Transform");
                               t.propertyPath = String(u8"position");
                               t.kind = propanim::TrackValueKind::Float3;
                               c.tracks.PushBack(Move(t));
                           });
                   });
        if (m_toolbar.Get() != nullptr)
        {
            m_toolbar->Refresh();
        }
    }

    void PropertyAnimationClipEditorPage::RequestRebuild()
    {
        ui::UIContext* ctx = (m_rows.Get() != nullptr) ? m_rows->Context : nullptr;
        if (ctx == nullptr)
        {
            Rebuild();
            return;
        }
        PropertyAnimationClipEditorPage* self = this;
        ctx->MutationQueueRef().QueueAction(Function<void()>{[self]() { self->Rebuild(); }});
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
