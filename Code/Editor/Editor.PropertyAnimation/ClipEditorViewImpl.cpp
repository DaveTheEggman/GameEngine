// Editor::PropertyAnimation - ClipEditorView implementation (UI construction + curve wiring). Kept
// out of the interface (heavy UI bodies; the module-hygiene rule).

module;
#include "Core/Prelude.h"
#include <cstdlib>

module editor.propertyanimation;

import foundation.core;
import foundation.ui;
import foundation.ui.toolkit;
import foundation.propertyanimation;
import editor.core;

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

    StringView ClipEditorView::KindName(propanim::TrackValueKind kind)
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

    StringView ClipEditorView::InterpName(CurveKeyInterpolation interp)
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

    ui::toolkit::CurveInterpolation ClipEditorView::ClipToCanvasInterp(CurveKeyInterpolation i)
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

    CurveKeyInterpolation ClipEditorView::CanvasToClipInterp(ui::toolkit::CurveInterpolation i)
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

    ClipEditorView::ClipEditorView(IClipEditorHost& host) : m_host(&host)
    {
        m_editDuration = Max(m_host->Clip().ComputeDuration(), 1.0f); // canvas time-axis scale

        m_scroll = MakeRef<ui::ScrollView>(DefaultAllocator());
        m_rows = MakeRef<ui::FlexLayout>(DefaultAllocator());
        m_rows->Direction = ui::Orientation::Vertical;
        m_rows->Spacing = 2.0f;
        m_scroll->AddView(m_rows.Get());
        Rebuild();
    }

    void ClipEditorView::ApplyState(const propanim::PropertyAnimationClip& state, bool rebuild)
    {
        m_host->Clip() = state;
        if (rebuild)
        {
            RequestRebuild();
        }
    }

    void ClipEditorView::PushClipEdit(propanim::PropertyAnimationClip before,
                                      propanim::PropertyAnimationClip after)
    {
        after.duration = Max(after.duration, after.ComputeDuration());
        (void)m_host->Commands().Execute(UniquePtr<IEditorCommand>(
            DefaultAllocator().New<ClipEditCommand>(*m_host, Move(before), Move(after)),
            DefaultAllocator()));
    }

    RefPtr<ui::FlexLayout> ClipEditorView::MakeRow(f32 indent, f32 height)
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

    ui::Button* ClipEditorView::MakeButton(ui::FlexLayout& row, StringView label, f32 width,
                                           Function<void()> onClick)
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

    void ClipEditorView::AddLabel(ui::FlexLayout& row, StringView text, f32 grow, f32 width)
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

    void ClipEditorView::AddTextField(ui::FlexLayout& row, StringView value,
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

    void ClipEditorView::AddFloatField(ui::FlexLayout& row, f32 value, Function<void(f32)> commit,
                                       f32 width)
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

    void ClipEditorView::SetScrubTime(f32 t)
    {
        m_scrubTime = t;
        RefreshPreview();
    }

    void ClipEditorView::BuildTransportRow()
    {
        // The scrub is owned by the host's Timeline widget now (the numeric field is retired); this row
        // keeps just the clip length + the sampled-value readout at the current scrub time.
        auto row = MakeRow(0.0f, 26.0f);
        ClipEditorView* self = this;
        AddLabel(*row, u8"Length", 0.0f, 52.0f);
        // Authoritative clip-duration edit: the host clamps to at least the last key + commits one undo
        // step, then the rebuild re-syncs this field + the timeline extent (Sedulous parity).
        AddFloatField(*row, m_editDuration,
                      [self](f32 d) { self->m_host->SetClipDuration((d > 1e-3f) ? d : 1.0f); }, 56.0f);

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

    void ClipEditorView::RefreshPreview()
    {
        if (m_preview.Get() == nullptr)
        {
            return;
        }
        // Sample every track at the scrub time; show "component.path=..." for each.
        String out;
        propanim::PropertyAnimationClip& clip = Clip();
        for (usize i = 0; i < clip.tracks.Size(); ++i)
        {
            const propanim::PropertyTrack& t = clip.tracks[i];
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

    void ClipEditorView::BuildTrackRows(usize trackIndex)
    {
        ClipEditorView* self = this;
        const propanim::PropertyTrack& track = Clip().tracks[trackIndex];

        String key = track.componentType;
        key += u8"|";
        key += track.propertyPath.AsView();
        const bool collapsed = IsTrackCollapsed(key.AsView());

        // --- track header: [caret] component + property + kind + remove ---
        auto header = MakeRow(0.0f, 26.0f);
        MakeButton(*header, collapsed ? u8">" : u8"v", 22.0f,
                   [self, k = String(key)]()
                   {
                       self->ToggleTrackCollapsed(k.AsView());
                       self->RequestRebuild(); // fold/unfold the track's rows
                   });
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

        if (collapsed)
        {
            return; // folded: header only
        }

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
                // Rotation is edited as EULER degrees (Vector3 X=pitch, Y=yaw, Z=roll), not raw
                // quaternion x/y/z/w - quaternion components are not human-editable. We convert to/from
                // the stored quaternion in the background (Sedulous parity). Gimbal ambiguity at the
                // poles is inherent to any euler UI; the stored data is still the exact quaternion.
                constexpr f32 kRadToDeg = 57.29577951f;
                constexpr f32 kDegToRad = 0.01745329252f;
                f32 yaw = 0.0f, pitch = 0.0f, roll = 0.0f;
                ToYawPitchRoll(qk.value, yaw, pitch, roll);
                const f32 eulerDeg[3] = {pitch * kRadToDeg, yaw * kRadToDeg, roll * kRadToDeg};
                for (u32 ci = 0; ci < 3; ++ci)
                {
                    AddFloatField(*krow, eulerDeg[ci],
                                  [self, trackIndex, ki, ci, yaw, pitch, roll](f32 vDeg)
                                  {
                                      self->Mutate(
                                          [&](propanim::PropertyAnimationClip& c)
                                          {
                                              f32 p = pitch, y = yaw, r = roll;
                                              const f32 v = vDeg * kDegToRad;
                                              (ci == 0 ? p : ci == 1 ? y : r) = v;
                                              c.tracks[trackIndex].quatKeys[ki].value =
                                                  FromYawPitchRoll(y, p, r);
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

    void ClipEditorView::AddCurveCanvas(usize trackIndex)
    {
        ClipEditorView* self = this;
        const propanim::PropertyTrack& track = Clip().tracks[trackIndex];
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
        // Ruling 2: the curve time axis is SECONDS matching the clip length (shares the dopesheet axis),
        // not a private normalized 0..1. Key times are stored + edited in absolute seconds.
        canvas->TimeSpan = (m_editDuration > 1e-3f) ? m_editDuration : 1.0f;

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
        canvas->OnEditBegin.Add(
            [self]()
            {
                self->m_gestureBefore = self->Clip();
                self->m_gestureDirty = false;
            });
        canvas->OnKeyChanged.Add(
            [self, trackIndex, raw](i32, i32) { self->WriteBackTrack(trackIndex, *raw); });
        canvas->OnKeyAdded.Add(
            [self, trackIndex, raw](i32, i32) { self->WriteBackTrack(trackIndex, *raw); });
        canvas->OnKeyRemoved.Add(
            [self, trackIndex, raw](i32, i32) { self->WriteBackTrack(trackIndex, *raw); });
        canvas->OnEditEnd.Add(
            [self]()
            {
                // A bare select-click fires begin/end with no key change: push nothing (no undo
                // step) so the selection - and its tangent handles - survive the mouse-up.
                if (!self->m_gestureDirty)
                {
                    return;
                }
                // One undo step per gesture. The live edits already mutated the clip AND the canvas
                // shows them, so the command applies `after` WITHOUT a rebuild (liveApplied) - a
                // rebuild would recreate the canvas and drop the selected key's tangent handles.
                propanim::PropertyAnimationClip after = self->Clip();
                after.duration = Max(after.duration, after.ComputeDuration()); // keep authored length
                (void)self->m_host->Commands().Execute(UniquePtr<IEditorCommand>(
                    DefaultAllocator().New<ClipEditCommand>(*self->m_host, Move(self->m_gestureBefore),
                                                            Move(after), /*liveApplied=*/true),
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

    void ClipEditorView::PushTrackToCanvas(usize trackIndex, ui::toolkit::CurveCanvas& canvas)
    {
        const propanim::PropertyTrack& track = Clip().tracks[trackIndex];
        const u32 channels = propanim::ChannelCount(track.kind);
        for (u32 ch = 0; ch < channels; ++ch)
        {
            Array<ui::toolkit::CurveCanvas::Key> keys;
            const Curve& cur = track.channels[ch];
            for (usize i = 0; i < cur.KeyCount(); ++i)
            {
                const CurveKey& k = cur.Keys()[i];
                // Absolute seconds - the canvas TimeSpan maps [0,duration] to width (ruling 2).
                keys.PushBack(
                    ui::toolkit::CurveCanvas::Key{k.time, k.value, k.tangentIn, k.tangentOut});
            }
            canvas.SetKeys(static_cast<i32>(ch),
                           Span<const ui::toolkit::CurveCanvas::Key>{keys.Data(), keys.Size()});
        }
    }

    void ClipEditorView::WriteBackTrack(usize trackIndex, ui::toolkit::CurveCanvas& canvas)
    {
        propanim::PropertyAnimationClip& clip = Clip();
        if (trackIndex >= clip.tracks.Size())
        {
            return;
        }
        propanim::PropertyTrack& track = clip.tracks[trackIndex];
        const u32 channels = propanim::ChannelCount(track.kind);
        for (u32 ch = 0; ch < channels; ++ch)
        {
            // The canvas carries ONE interpolation per channel (a P1 simplification), so write-back
            // FLATTENS it onto every key of the channel - authored per-KEY interpolation is not
            // preserved through an edit. (Per-key interp is deferred canvas polish.)
            const CurveKeyInterpolation interp =
                CanvasToClipInterp(canvas.GetChannelDescriptor(static_cast<i32>(ch)).Interpolation);
            const i32 n = canvas.GetKeyCount(static_cast<i32>(ch));
            track.channels[ch].Clear();
            for (i32 i = 0; i < n; ++i)
            {
                const ui::toolkit::CurveCanvas::Key k = canvas.GetKey(static_cast<i32>(ch), i);
                CurveKey ck;
                ck.time = k.Time; // absolute seconds (ruling 2)
                ck.value = k.Value;
                ck.tangentIn = k.TangentIn;
                ck.tangentOut = k.TangentOut;
                ck.interpolation = interp;
                track.channels[ch].AddKey(ck);
            }
        }
        clip.duration = Max(clip.duration, clip.ComputeDuration()); // preserve an authored length
        m_gestureDirty = true; // a key actually moved/added/removed this gesture
        m_host->MarkClipDirty();
        RefreshPreview();
    }

    void ClipEditorView::Rebuild()
    {
        // Keep the canvas time axis + the Length field in step with the clip's authored duration.
        m_editDuration = Max(Max(Clip().duration, Clip().ComputeDuration()), 1.0f);
        m_rows->RemoveAllViews();
        BuildTransportRow();
        propanim::PropertyAnimationClip& clip = Clip();
        for (usize i = 0; i < clip.tracks.Size(); ++i)
        {
            BuildTrackRows(i);
        }
        // "+ Track" lives on the panel now (it needs the scene + selection to offer a property picker).
        m_host->OnClipViewRebuilt();
    }

    bool ClipEditorView::IsTrackCollapsed(StringView key) const
    {
        for (const String& k : m_collapsedTracks)
        {
            if (k.AsView() == key)
            {
                return true;
            }
        }
        return false;
    }

    void ClipEditorView::ToggleTrackCollapsed(StringView key)
    {
        for (usize i = 0; i < m_collapsedTracks.Size(); ++i)
        {
            if (m_collapsedTracks[i].AsView() == key)
            {
                m_collapsedTracks.RemoveAt(i);
                return;
            }
        }
        m_collapsedTracks.PushBack(String(key));
    }

    void ClipEditorView::AddTrack(StringView componentType, StringView propertyPath,
                                  propanim::TrackValueKind kind)
    {
        Mutate(
            [&](propanim::PropertyAnimationClip& c)
            {
                propanim::PropertyTrack t;
                t.componentType = String(componentType);
                t.propertyPath = String(propertyPath);
                t.kind = kind;
                c.tracks.PushBack(Move(t));
            });
    }

    void ClipEditorView::ResetForClip()
    {
        m_editDuration = Max(Clip().ComputeDuration(), 1.0f);
        m_scrubTime = 0.0f;
        Rebuild();
    }

    void ClipEditorView::RequestRebuild()
    {
        ui::UIContext* ctx = (m_rows.Get() != nullptr) ? m_rows->Context : nullptr;
        if (ctx == nullptr)
        {
            Rebuild();
            return;
        }
        ClipEditorView* self = this;
        ctx->MutationQueueRef().QueueAction(Function<void()>{[self]() { self->Rebuild(); }});
    }
}
