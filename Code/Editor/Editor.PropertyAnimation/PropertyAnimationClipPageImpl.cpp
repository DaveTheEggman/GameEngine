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
        constexpr CurveInterpolation kInterps[] = {
            CurveInterpolation::Constant, CurveInterpolation::Linear, CurveInterpolation::Cubic};

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

    StringView PropertyAnimationClipEditorPage::InterpName(CurveInterpolation interp)
    {
        switch (interp)
        {
        case CurveInterpolation::Constant:
            return u8"Step";
        case CurveInterpolation::Linear:
            return u8"Linear";
        case CurveInterpolation::Cubic:
            return u8"Cubic";
        }
        return u8"?";
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
        String dur(u8"/ ");
        AppendValue(dur, m_clip.duration);
        dur += u8"s";
        AddLabel(*row, dur.AsView(), 0.0f, 80.0f);

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
        const u32 channels = propanim::ChannelCount(track.kind);
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
            // Scalar-channel tracks: channel[0] is the master timeline; each row edits every channel's
            // value at that key index (P1 keeps channel key times aligned).
            const usize keyCount = (channels > 0) ? track.channels[0].KeyCount() : 0;
            for (usize ki = 0; ki < keyCount; ++ki)
            {
                auto krow = MakeRow(18.0f);
                const CurveKey& k0 = track.channels[0].Keys()[ki];
                AddFloatField(*krow, k0.time,
                              [self, trackIndex, ki, channels](f32 t)
                              {
                                  self->Mutate(
                                      [&](propanim::PropertyAnimationClip& c)
                                      {
                                          propanim::PropertyTrack& tr = c.tracks[trackIndex];
                                          for (u32 ch = 0; ch < channels; ++ch)
                                          {
                                              if (ki < tr.channels[ch].KeyCount())
                                              {
                                                  tr.channels[ch].Keys()[ki].time = t;
                                              }
                                          }
                                      });
                              },
                              50.0f);
                for (u32 ch = 0; ch < channels; ++ch)
                {
                    const f32 val =
                        (ki < track.channels[ch].KeyCount()) ? track.channels[ch].Keys()[ki].value : 0.0f;
                    AddLabel(*krow, ChannelLabel(track.kind, ch), 0.0f, 12.0f);
                    AddFloatField(*krow, val,
                                  [self, trackIndex, ki, ch](f32 v)
                                  {
                                      self->Mutate(
                                          [&](propanim::PropertyAnimationClip& c)
                                          {
                                              Curve& cur = c.tracks[trackIndex].channels[ch];
                                              if (ki < cur.KeyCount())
                                              {
                                                  cur.Keys()[ki].value = v;
                                              }
                                          });
                                  },
                                  52.0f);
                }
                MakeButton(*krow, InterpName(k0.interpolation), 56.0f,
                           [self, trackIndex, ki, channels]()
                           {
                               self->Mutate(
                                   [&](propanim::PropertyAnimationClip& c)
                                   {
                                       propanim::PropertyTrack& tr = c.tracks[trackIndex];
                                       CurveInterpolation next = CurveInterpolation::Linear;
                                       if (ki < tr.channels[0].KeyCount())
                                       {
                                           const CurveInterpolation cur =
                                               tr.channels[0].Keys()[ki].interpolation;
                                           usize idx = 0;
                                           for (usize j = 0; j < 3; ++j)
                                           {
                                               if (kInterps[j] == cur)
                                               {
                                                   idx = j;
                                                   break;
                                               }
                                           }
                                           next = kInterps[(idx + 1) % 3];
                                       }
                                       for (u32 ch = 0; ch < channels; ++ch)
                                       {
                                           if (ki < tr.channels[ch].KeyCount())
                                           {
                                               tr.channels[ch].Keys()[ki].interpolation = next;
                                           }
                                       }
                                   });
                           });
                MakeButton(*krow, u8"x", 22.0f,
                           [self, trackIndex, ki, channels]()
                           {
                               self->Mutate(
                                   [&](propanim::PropertyAnimationClip& c)
                                   {
                                       propanim::PropertyTrack& tr = c.tracks[trackIndex];
                                       for (u32 ch = 0; ch < channels; ++ch)
                                       {
                                           if (ki < tr.channels[ch].KeyCount())
                                           {
                                               tr.channels[ch].Keys().RemoveAt(ki);
                                           }
                                       }
                                   });
                           });
            }
            MakeButton(*MakeRow(18.0f), u8"+ Key", 60.0f,
                       [self, trackIndex, channels]()
                       {
                           self->Mutate(
                               [&](propanim::PropertyAnimationClip& c)
                               {
                                   propanim::PropertyTrack& tr = c.tracks[trackIndex];
                                   f32 at = 0.0f;
                                   if (channels > 0 && tr.channels[0].KeyCount() > 0)
                                   {
                                       at = tr.channels[0].Duration() + 1.0f;
                                   }
                                   for (u32 ch = 0; ch < channels; ++ch)
                                   {
                                       CurveKey k;
                                       k.time = at;
                                       tr.channels[ch].AddKey(k);
                                   }
                               });
                       });
        }
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
