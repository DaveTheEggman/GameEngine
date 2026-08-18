// Editor::PropertyAnimation - ClipEditorView implementation (UI construction + curve wiring). Kept
// out of the interface (heavy UI bodies; the module-hygiene rule).

module;
#include "Core/Prelude.h"
#include "Core/Log/Log.h"
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
        // Canvas time-axis scale: the AUTHORED length wins when longer than the key extent, so
        // the curve axis always matches the timeline ruler (an authored 3 s clip with keys only
        // to 1 s still shows 3 s everywhere).
        m_editDuration =
            Max(Max(m_host->Clip().duration, m_host->Clip().ComputeDuration()), 1.0f);

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

    namespace
    {
        // One track's value at `time`, formatted per kind. Rotation shows EULER DEGREES
        // (pitch,yaw,roll - matching the key rows' edit fields), never raw quaternion parts.
        void AppendSampledValue(String& out, const propanim::PropertyTrack& t, f32 time)
        {
            const Variant v = t.Sample(time);
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
            {
                constexpr f32 kRadToDeg = 57.29577951f;
                f32 yaw = 0.0f, pitch = 0.0f, roll = 0.0f;
                ToYawPitchRoll(v.Get<Quaternion>(), yaw, pitch, roll);
                out += u8"(";
                AppendValue(out, pitch * kRadToDeg);
                out += u8",";
                AppendValue(out, yaw * kRadToDeg);
                out += u8",";
                AppendValue(out, roll * kRadToDeg);
                out += u8")deg";
                break;
            }
            }
        }
    }

    void ClipEditorView::RefreshPreview()
    {
        if (m_preview.Get() == nullptr)
        {
            return;
        }
        String out;
        propanim::PropertyAnimationClip& clip = Clip();

        // The SELECTED key first (canvas or dopesheet pick): its track, time, and value there.
        if (m_previewSelActive && m_previewSelTrack < clip.tracks.Size())
        {
            const propanim::PropertyTrack& t = clip.tracks[m_previewSelTrack];
            out += u8"sel ";
            out += t.propertyPath.AsView();
            if (m_previewSelChannel >= 0)
            {
                out += u8".";
                out += ChannelLabel(t.kind, static_cast<u32>(m_previewSelChannel));
            }
            out += u8" @";
            AppendValue(out, m_previewSelTime);
            out += u8"s=";
            AppendSampledValue(out, t, m_previewSelTime);
            out += u8"   |   ";
        }

        // Then every track sampled at the scrub time: "path=value".
        for (usize i = 0; i < clip.tracks.Size(); ++i)
        {
            const propanim::PropertyTrack& t = clip.tracks[i];
            if (i > 0)
            {
                out += u8"   ";
            }
            out += t.propertyPath.AsView();
            out += u8"=";
            AppendSampledValue(out, t, m_scrubTime);
        }
        m_preview->SetText(out.AsView());
    }

    void ClipEditorView::ShowSelectedKey(usize trackIndex, i32 channel, f32 time)
    {
        m_previewSelActive = true;
        m_previewSelTrack = trackIndex;
        m_previewSelChannel = channel;
        m_previewSelTime = time;
        RefreshPreview();
        RequestInspectorRefresh(); // typed fields re-target (deferred: mid-dispatch safe)
    }

    void ClipEditorView::ClearSelectedKey()
    {
        if (!m_previewSelActive)
        {
            return;
        }
        m_previewSelActive = false;
        RefreshPreview();
        RequestInspectorRefresh();
    }

    // === the selected-track surface (Sedulous shape: the dopesheet IS the track list; the
    // area below shows the SELECTED track's strip + keyframe inspector + one curve canvas) ===

    namespace
    {
        inline constexpr f32 kKeyTimeEps = 1e-4f;

        [[nodiscard]] i32 FindChannelKeyAt(const propanim::PropertyTrack& track, u32 channel,
                                           f32 time)
        {
            const Curve& cur = track.channels[channel];
            for (usize i = 0; i < cur.KeyCount(); ++i)
            {
                if (Abs(cur.Keys()[i].time - time) < kKeyTimeEps)
                {
                    return static_cast<i32>(i);
                }
            }
            return -1;
        }

        [[nodiscard]] i32 FindQuatKeyAt(const propanim::PropertyTrack& track, f32 time)
        {
            for (usize i = 0; i < track.quatKeys.Size(); ++i)
            {
                if (Abs(track.quatKeys[i].time - time) < kKeyTimeEps)
                {
                    return static_cast<i32>(i);
                }
            }
            return -1;
        }

        // Retime every channel/quat key sitting at `t0` to `t1`, keeping arrays time-sorted.
        void RetimeTrackKeysAt(propanim::PropertyTrack& track, f32 t0, f32 t1)
        {
            const u32 channels = propanim::ChannelCount(track.kind);
            for (u32 ch = 0; ch < channels; ++ch)
            {
                Curve& cur = track.channels[ch];
                Array<CurveKey> keys;
                for (usize i = 0; i < cur.KeyCount(); ++i)
                {
                    CurveKey k = cur.Keys()[i];
                    if (Abs(k.time - t0) < kKeyTimeEps)
                    {
                        k.time = t1;
                    }
                    keys.PushBack(k);
                }
                cur.Clear();
                for (const CurveKey& k : keys)
                {
                    cur.AddKey(k); // AddKey keeps time order
                }
            }
            for (usize i = 0; i < track.quatKeys.Size(); ++i)
            {
                if (Abs(track.quatKeys[i].time - t0) < kKeyTimeEps)
                {
                    track.quatKeys[i].time = t1;
                }
            }
            for (usize i = 1; i < track.quatKeys.Size(); ++i) // insertion sort (small arrays)
            {
                const propanim::QuatKey k = track.quatKeys[i];
                usize j = i;
                while (j > 0 && track.quatKeys[j - 1].time > k.time)
                {
                    track.quatKeys[j] = track.quatKeys[j - 1];
                    --j;
                }
                track.quatKeys[j] = k;
            }
        }

        // Set-or-insert a channel key at `time` (scene capture + inspector commits). New keys
        // inherit the track's interpolation (first key's; Linear on an empty channel).
        void UpsertChannelKeyAt(propanim::PropertyTrack& track, u32 channel, f32 time, f32 value)
        {
            Curve& cur = track.channels[channel];
            const i32 at = FindChannelKeyAt(track, channel, time);
            if (at >= 0)
            {
                cur.Keys()[static_cast<usize>(at)].value = value;
                return;
            }
            CurveKey k;
            k.time = time;
            k.value = value;
            k.interpolation = cur.KeyCount() > 0 ? cur.Keys()[0].interpolation
                                                 : CurveKeyInterpolation::Linear;
            cur.AddKey(k);
        }

        void UpsertQuatKeyAt(propanim::PropertyTrack& track, f32 time, const Quaternion& value)
        {
            const i32 at = FindQuatKeyAt(track, time);
            if (at >= 0)
            {
                track.quatKeys[static_cast<usize>(at)].value = value;
                return;
            }
            track.quatKeys.PushBack(propanim::QuatKey{time, value});
            for (usize i = track.quatKeys.Size() - 1;
                 i > 0 && track.quatKeys[i - 1].time > track.quatKeys[i].time; --i)
            {
                const propanim::QuatKey tmp = track.quatKeys[i - 1];
                track.quatKeys[i - 1] = track.quatKeys[i];
                track.quatKeys[i] = tmp;
            }
        }

        // Decompose a captured Variant into per-channel key writes at `time`. False = the scene
        // value's type does not match the track kind (caller warns + skips).
        [[nodiscard]] bool UpsertTrackValueAt(propanim::PropertyTrack& track, f32 time,
                                              const Variant& v)
        {
            switch (track.kind)
            {
            case propanim::TrackValueKind::Float:
                if (!v.Is<f32>())
                {
                    return false;
                }
                UpsertChannelKeyAt(track, 0, time, v.Get<f32>());
                return true;
            case propanim::TrackValueKind::Float3:
            {
                if (!v.Is<Float3>())
                {
                    return false;
                }
                const Float3 f = v.Get<Float3>();
                UpsertChannelKeyAt(track, 0, time, f.x);
                UpsertChannelKeyAt(track, 1, time, f.y);
                UpsertChannelKeyAt(track, 2, time, f.z);
                return true;
            }
            case propanim::TrackValueKind::Color:
            {
                if (!v.Is<Color>())
                {
                    return false;
                }
                const Color c = v.Get<Color>();
                UpsertChannelKeyAt(track, 0, time, c.r);
                UpsertChannelKeyAt(track, 1, time, c.g);
                UpsertChannelKeyAt(track, 2, time, c.b);
                UpsertChannelKeyAt(track, 3, time, c.a);
                return true;
            }
            case propanim::TrackValueKind::Quat:
                if (!v.Is<Quaternion>())
                {
                    return false;
                }
                UpsertQuatKeyAt(track, time, v.Get<Quaternion>());
                return true;
            }
            return false;
        }
    }

    void ClipEditorView::SetSelectedTrack(i32 trackIndex)
    {
        const i32 clamped =
            (trackIndex >= 0 && trackIndex < static_cast<i32>(Clip().tracks.Size())) ? trackIndex
                                                                                     : -1;
        if (clamped == m_selectedTrack)
        {
            return;
        }
        m_selectedTrack = clamped;
        if (m_previewSelActive && static_cast<i32>(m_previewSelTrack) != clamped)
        {
            m_previewSelActive = false; // the previous track's key pick is stale here
        }
        RequestRebuild(); // strip + inspector + canvas all re-target
    }

    void ClipEditorView::BuildSelectedTrackStrip()
    {
        ClipEditorView* self = this;
        propanim::PropertyAnimationClip& clip = Clip();
        auto strip = MakeRow(0.0f, 26.0f);
        if (clip.tracks.IsEmpty())
        {
            AddLabel(*strip, u8"(no tracks - + Track adds one)", 1.0f);
            return;
        }
        if (m_selectedTrack < 0 || m_selectedTrack >= static_cast<i32>(clip.tracks.Size()))
        {
            AddLabel(*strip, u8"(select a track in the dopesheet)", 1.0f);
            return;
        }
        const usize trackIndex = static_cast<usize>(m_selectedTrack);
        const propanim::PropertyTrack& track = clip.tracks[trackIndex];

        // ONE "Component.property.path" field (user ruling): component type names never contain
        // dots, so the FIRST dot splits; dot-less text edits just the property path.
        {
            String path = track.componentType;
            path += u8".";
            path += track.propertyPath.AsView();
            AddTextField(*strip, path.AsView(),
                         [self, trackIndex](StringView v)
                         {
                             usize dot = 0;
                             while (dot < v.Size() && v[dot] != u8'.')
                             {
                                 ++dot;
                             }
                             const bool hasDot = dot < v.Size();
                             self->Mutate(
                                 [&](propanim::PropertyAnimationClip& c)
                                 {
                                     propanim::PropertyTrack& t = c.tracks[trackIndex];
                                     if (hasDot)
                                     {
                                         t.componentType = String(v.SubStr(0, dot));
                                         t.propertyPath =
                                             String(v.SubStr(dot + 1, v.Size() - dot - 1));
                                     }
                                     else
                                     {
                                         t.propertyPath = String(v);
                                     }
                                 });
                         },
                         190.0f);
        }
        MakeButton(*strip, KindName(track.kind), 58.0f,
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
        if (track.kind != propanim::TrackValueKind::Quat)
        {
            const CurveKeyInterpolation cur =
                (track.channels[0].KeyCount() > 0) ? track.channels[0].Keys()[0].interpolation
                                                   : CurveKeyInterpolation::Linear;
            MakeButton(*strip, InterpName(cur), 64.0f,
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
        }
        // Key-from-scene capture (the PRIMARY value workflow: pose the entity, press Key).
        MakeButton(*strip, u8"Key", 44.0f,
                   [self, trackIndex]() { self->KeyTrackFromScene(trackIndex); });
        MakeButton(*strip, u8"Key All", 64.0f, [self]() { self->KeyAllFromScene(); });
        MakeButton(*strip, u8"x", 22.0f,
                   [self, trackIndex]()
                   {
                       self->ClearSelectedKey();
                       self->Mutate([&](propanim::PropertyAnimationClip& c)
                                    { c.tracks.RemoveAt(trackIndex); });
                       self->m_selectedTrack =
                           Min(self->m_selectedTrack,
                               static_cast<i32>(self->Clip().tracks.Size()) - 1);
                   });
    }

    void ClipEditorView::BuildKeyInspectorHost()
    {
        m_inspectorRow = MakeRow(0.0f, 26.0f); // persistent host; children swap per selection
        RefreshKeyInspector();
    }

    void ClipEditorView::RefreshKeyInspector()
    {
        if (m_inspectorRow.Get() == nullptr)
        {
            return;
        }
        m_inspectorRow->RemoveAllViews();
        propanim::PropertyAnimationClip& clip = Clip();
        if (!m_previewSelActive || m_previewSelTrack >= clip.tracks.Size())
        {
            AddLabel(*m_inspectorRow,
                     u8"(click a key to edit; Key captures the scene value at the playhead)", 1.0f);
            return;
        }
        ClipEditorView* self = this;
        const usize trackIndex = m_previewSelTrack;
        const propanim::PropertyTrack& track = clip.tracks[trackIndex];
        const f32 selTime = m_previewSelTime;

        // Del first: removes the selected key across every channel (and the quat list) at
        // the selected time - quat tracks previously had NO delete path at all.
        MakeButton(*m_inspectorRow, u8"Del", 36.0f,
                   [self, trackIndex, selTime]()
                   {
                       self->ClearSelectedKey();
                       self->Mutate(
                           [&](propanim::PropertyAnimationClip& c)
                           {
                               if (trackIndex >= c.tracks.Size())
                               {
                                   return;
                               }
                               propanim::PropertyTrack& t = c.tracks[trackIndex];
                               const u32 chn = propanim::ChannelCount(t.kind);
                               for (u32 ch = 0; ch < chn; ++ch)
                               {
                                   const i32 at = FindChannelKeyAt(t, ch, selTime);
                                   if (at >= 0)
                                   {
                                       Curve& cur = t.channels[ch];
                                       Array<CurveKey> keep;
                                       for (usize i = 0; i < cur.KeyCount(); ++i)
                                       {
                                           if (static_cast<i32>(i) != at)
                                           {
                                               keep.PushBack(cur.Keys()[i]);
                                           }
                                       }
                                       cur.Clear();
                                       for (const CurveKey& k : keep)
                                       {
                                           cur.AddKey(k);
                                       }
                                   }
                               }
                               const i32 qat = FindQuatKeyAt(t, selTime);
                               if (qat >= 0)
                               {
                                   t.quatKeys.RemoveAt(static_cast<usize>(qat));
                               }
                           });
                   });

        // Time: retimes every channel key at the selected time (a dopesheet diamond = the
        // whole track at that time), one undo step.
        AddLabel(*m_inspectorRow, u8"Time", 0.0f, 34.0f);
        AddFloatField(*m_inspectorRow, selTime,
                      [self, trackIndex, selTime](f32 t)
                      {
                          const f32 clampedT = Max(t, 0.0f);
                          self->Mutate(
                              [&](propanim::PropertyAnimationClip& c)
                              {
                                  if (trackIndex < c.tracks.Size())
                                  {
                                      RetimeTrackKeysAt(c.tracks[trackIndex], selTime, clampedT);
                                  }
                              });
                          self->m_previewSelTime = clampedT;
                      },
                      52.0f);

        // Value: exact numeric entry per component (the Sedulous keyframe inspector, over our
        // undo stack). Commits UPSERT at the selected time, so a channel missing a key there
        // gains one instead of dropping the edit.
        const auto addChannelField =
            [self, trackIndex, selTime, &track](ui::FlexLayout& row, u32 channel, StringView name)
        {
            const i32 at = FindChannelKeyAt(track, channel, selTime);
            const f32 shown = (at >= 0)
                                  ? track.channels[channel].Keys()[static_cast<usize>(at)].value
                                  : 0.0f;
            self->AddLabel(row, name, 0.0f, 16.0f);
            self->AddFloatField(row, shown,
                                [self, trackIndex, selTime, channel](f32 v)
                                {
                                    self->Mutate(
                                        [&](propanim::PropertyAnimationClip& c)
                                        {
                                            if (trackIndex < c.tracks.Size())
                                            {
                                                UpsertChannelKeyAt(c.tracks[trackIndex], channel,
                                                                   selTime, v);
                                            }
                                        });
                                },
                                56.0f);
        };

        switch (track.kind)
        {
        case propanim::TrackValueKind::Float:
            addChannelField(*m_inspectorRow, 0, u8"V");
            break;
        case propanim::TrackValueKind::Float3:
            addChannelField(*m_inspectorRow, 0, u8"X");
            addChannelField(*m_inspectorRow, 1, u8"Y");
            addChannelField(*m_inspectorRow, 2, u8"Z");
            break;
        case propanim::TrackValueKind::Color:
            addChannelField(*m_inspectorRow, 0, u8"R");
            addChannelField(*m_inspectorRow, 1, u8"G");
            addChannelField(*m_inspectorRow, 2, u8"B");
            addChannelField(*m_inspectorRow, 3, u8"A");
            break;
        case propanim::TrackValueKind::Quat:
        {
            // Euler degrees (pitch/yaw/roll), converted to/from the stored quaternion - the
            // same convention as the value readout and the old key rows.
            constexpr f32 kRadToDeg = 57.29577951f;
            constexpr f32 kDegToRad = 0.01745329252f;
            const i32 at = FindQuatKeyAt(track, selTime);
            Quaternion q = (at >= 0) ? track.quatKeys[static_cast<usize>(at)].value
                                     : Quaternion::Identity;
            f32 yaw = 0.0f, pitch = 0.0f, roll = 0.0f;
            ToYawPitchRoll(q, yaw, pitch, roll);
            const f32 eulerDeg[3] = {pitch * kRadToDeg, yaw * kRadToDeg, roll * kRadToDeg};
            const StringView names[3] = {u8"P", u8"Y", u8"R"};
            for (u32 ci = 0; ci < 3; ++ci)
            {
                AddLabel(*m_inspectorRow, names[ci], 0.0f, 16.0f);
                AddFloatField(*m_inspectorRow, eulerDeg[ci],
                              [self, trackIndex, selTime, ci, yaw, pitch, roll](f32 vDeg)
                              {
                                  f32 p = pitch, y = yaw, r = roll;
                                  const f32 v = vDeg * kDegToRad;
                                  (ci == 0 ? p : ci == 1 ? y : r) = v;
                                  const Quaternion nq = FromYawPitchRoll(y, p, r);
                                  self->Mutate(
                                      [&](propanim::PropertyAnimationClip& c)
                                      {
                                          if (trackIndex < c.tracks.Size())
                                          {
                                              UpsertQuatKeyAt(c.tracks[trackIndex], selTime, nq);
                                          }
                                      });
                              },
                              56.0f);
            }
            break;
        }
        }
    }

    void ClipEditorView::BuildQuatKeysRow(usize trackIndex)
    {
        ClipEditorView* self = this;
        const propanim::PropertyTrack& track = Clip().tracks[trackIndex];
        auto row = MakeRow(0.0f, 24.0f);
        AddLabel(*row, u8"Keys", 0.0f, 34.0f);
        if (track.quatKeys.IsEmpty())
        {
            AddLabel(*row, u8"(none - Key captures the pose at the playhead)", 1.0f);
            return;
        }
        for (usize i = 0; i < track.quatKeys.Size(); ++i)
        {
            const f32 keyTime = track.quatKeys[i].time;
            const bool isSelected = m_previewSelActive && m_previewSelTrack == trackIndex &&
                                    Abs(m_previewSelTime - keyTime) < kKeyTimeEps;
            String chip;
            chip += isSelected ? u8"[@" : u8"@";
            AppendValue(chip, keyTime);
            if (isSelected)
            {
                chip += u8"]";
            }
            MakeButton(*row, chip.AsView(), 58.0f,
                       [self, trackIndex, keyTime]()
                       {
                           self->ShowSelectedKey(trackIndex, -1, keyTime);
                           self->RequestRebuild(); // move the [selected] marker to this chip
                       });
        }
    }

    void ClipEditorView::RequestInspectorRefresh()
    {
        ui::UIContext* ctx = (m_rows.Get() != nullptr) ? m_rows->Context : nullptr;
        if (ctx == nullptr)
        {
            RefreshKeyInspector();
            return;
        }
        ClipEditorView* self = this;
        ctx->MutationQueueRef().QueueAction(
            Function<void()>{[self]() { self->RefreshKeyInspector(); }});
    }

    void ClipEditorView::KeyTrackFromScene(usize trackIndex)
    {
        if (trackIndex >= Clip().tracks.Size())
        {
            return;
        }
        const propanim::PropertyTrack& track = Clip().tracks[trackIndex];
        const Variant value =
            m_host->ReadSceneValue(track.componentType.AsView(), track.propertyPath.AsView());
        if (value.IsEmpty())
        {
            LOG_WARNING(u8"PropertyAnimation",
                        u8"Key: no scene value for {}.{} (is the bound entity selected?)",
                        track.componentType, track.propertyPath);
            return;
        }
        const f32 at = m_scrubTime;
        bool matched = false;
        Mutate(
            [&](propanim::PropertyAnimationClip& c)
            {
                if (trackIndex < c.tracks.Size())
                {
                    matched = UpsertTrackValueAt(c.tracks[trackIndex], at, value);
                }
            });
        if (!matched)
        {
            LOG_WARNING(u8"PropertyAnimation", u8"Key: scene value type mismatch for {}.{}",
                        track.componentType, track.propertyPath);
        }
        ShowSelectedKey(trackIndex, -1, at);
    }

    void ClipEditorView::KeyAllFromScene()
    {
        // Read every track's scene value FIRST, then write all captured ones as ONE undo step.
        propanim::PropertyAnimationClip& clip = Clip();
        Array<Variant> values;
        usize captured = 0;
        for (const propanim::PropertyTrack& track : clip.tracks)
        {
            Variant v =
                m_host->ReadSceneValue(track.componentType.AsView(), track.propertyPath.AsView());
            if (!v.IsEmpty())
            {
                ++captured;
            }
            values.PushBack(Move(v));
        }
        if (captured == 0)
        {
            LOG_WARNING(u8"PropertyAnimation",
                        u8"Key All: no track resolved a scene value (is the bound entity "
                        u8"selected?)");
            return;
        }
        const f32 at = m_scrubTime;
        Mutate(
            [&](propanim::PropertyAnimationClip& c)
            {
                for (usize i = 0; i < c.tracks.Size() && i < values.Size(); ++i)
                {
                    if (!values[i].IsEmpty())
                    {
                        (void)UpsertTrackValueAt(c.tracks[i], at, values[i]);
                    }
                }
            });
    }

    void ClipEditorView::SyncCanvasTransform()
    {
        if (m_curveCanvas == nullptr)
        {
            return;
        }
        const IClipEditorHost::TimeAxis axis = m_host->ClipTimeTransform();
        // Only pps/scroll change on zoom/pan (the gutter inset is fixed at build). Invalidate on change
        // only - an idle canvas triggers no redraw (A6 damage gate).
        if (m_curveCanvas->PixelsPerSecond != axis.pixelsPerSecond ||
            m_curveCanvas->ScrollSeconds != axis.scrollSeconds)
        {
            m_curveCanvas->PixelsPerSecond = axis.pixelsPerSecond;
            m_curveCanvas->ScrollSeconds = axis.scrollSeconds;
            m_curveCanvas->Invalidate();
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

        auto canvas = MakeRef<ui::toolkit::CurveCanvas>(DefaultAllocator());
        canvas->MaxKeys = 64;
        canvas->AutoFitValueRange = true;
        canvas->LinkedTime = channels > 1;
        // Ruling 2: the curve time axis is SECONDS matching the clip length (shares the dopesheet axis),
        // not a private normalized 0..1. Key times are stored + edited in absolute seconds.
        canvas->TimeSpan = (m_editDuration > 1e-3f) ? m_editDuration : 1.0f;
        // D1: consume the Timeline's shared seconds<->pixels transform (zoom/scroll), so the curve
        // follows the dopesheet at any zoom/pan and stays aligned under it.
        const IClipEditorHost::TimeAxis axis = m_host->ClipTimeTransform();
        canvas->UseSharedTimeTransform = true;
        canvas->PixelsPerSecond = axis.pixelsPerSecond;
        canvas->ScrollSeconds = axis.scrollSeconds;

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
        m_curveCanvas = raw; // D1 sync target (the panel pushes zoom/scroll here on Timeline changes)
        canvas->OnEditBegin.Add(
            [self]()
            {
                self->m_gestureBefore = self->Clip();
                self->m_gestureDirty = false;
            });
        canvas->OnKeyChanged.Add(
            [self, trackIndex, raw](i32 ch, i32 ki)
            {
                // A drag moves the selected key: keep the readout's time/value current. A
                // LINKED-TIME drag fires once per channel - only the canvas's own selected
                // key updates the readout, or the last channel (Z) would win every click.
                if (ch == raw->SelectedChannel() && ki == raw->SelectedKeyIndex() && ki >= 0 &&
                    ki < raw->GetKeyCount(ch))
                {
                    self->m_previewSelActive = true;
                    self->m_previewSelTrack = trackIndex;
                    self->m_previewSelChannel = ch;
                    self->m_previewSelTime = raw->GetKey(ch, ki).Time;
                }
                self->WriteBackTrack(trackIndex, *raw);
            });
        canvas->OnKeyAdded.Add(
            [self, trackIndex, raw](i32, i32) { self->WriteBackTrack(trackIndex, *raw); });
        canvas->OnKeyRemoved.Add(
            [self, trackIndex, raw](i32, i32) { self->WriteBackTrack(trackIndex, *raw); });
        canvas->OnSelectionChanged.Add(
            [self, trackIndex, raw](i32 ch, i32 ki)
            {
                // Selection is selection: ANY key pick updates the value readout (the old
                // behavior only *looked* selection-driven because the scrub sat at 0 and
                // write-backs resampled there - the first key's value by coincidence).
                if (ch >= 0 && ki >= 0 && ki < raw->GetKeyCount(ch))
                {
                    self->ShowSelectedKey(trackIndex, ch, raw->GetKey(ch, ki).Time);
                }
                else
                {
                    self->ClearSelectedKey();
                }
            });
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
        // Inset the canvas by the dopesheet's label-column gutter so t=0 sits at the same screen x as
        // the lanes above - the curve lines up under the dopesheet (D1). No right inset (fills the rest).
        wrap->Padding = ui::Thickness{axis.labelColumnWidth, 0.0f, 0.0f, 0.0f};
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
        propanim::PropertyAnimationClip& clip = Clip();
        if (m_selectedTrack >= static_cast<i32>(clip.tracks.Size()))
        {
            m_selectedTrack = static_cast<i32>(clip.tracks.Size()) - 1; // clamp after removals
        }
        if (m_selectedTrack < 0 && !clip.tracks.IsEmpty())
        {
            m_selectedTrack = 0; // something is always selected when tracks exist
        }
        m_rows->RemoveAllViews();
        m_inspectorRow = nullptr;  // rebuilt below (the old row was just destroyed)
        m_curveCanvas = nullptr;   // the canvas was just destroyed; AddCurveCanvas re-sets it if built
        BuildTransportRow();
        // The dopesheet (in the panel) is the track LIST; this area is the SELECTED track only.
        BuildSelectedTrackStrip();
        BuildKeyInspectorHost();
        if (m_selectedTrack >= 0 && m_selectedTrack < static_cast<i32>(clip.tracks.Size()))
        {
            if (clip.tracks[static_cast<usize>(m_selectedTrack)].kind !=
                propanim::TrackValueKind::Quat)
            {
                // ONE curve canvas, for the selected scalar track (interpolation shaping).
                AddCurveCanvas(static_cast<usize>(m_selectedTrack));
            }
            else
            {
                // Quat tracks have no canvas - the keys strip shows every keyframe as a
                // clickable chip (UAT: rotation keys were invisible below the dopesheet).
                BuildQuatKeysRow(static_cast<usize>(m_selectedTrack));
            }
        }
        // "+ Track" lives on the panel now (it needs the scene + selection to offer a property picker).
        m_host->OnClipViewRebuilt();
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
        // The new track becomes the working track (its strip/canvas show on the queued rebuild).
        m_selectedTrack = static_cast<i32>(Clip().tracks.Size()) - 1;
    }

    void ClipEditorView::ResetForClip()
    {
        m_editDuration = Max(Max(Clip().duration, Clip().ComputeDuration()), 1.0f);
        m_scrubTime = 0.0f;
        m_previewSelActive = false;
        m_selectedTrack = Clip().tracks.IsEmpty() ? -1 : 0;
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
