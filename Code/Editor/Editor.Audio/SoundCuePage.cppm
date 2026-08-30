// SPDX-License-Identifier: MIT
// Copyright (c) 2026-Present Robert Campbell

// Editor::Audio - the `:sound_cue_page` partition.
//
// SoundCuePage: the cue editor - eight variant slot rows (clip picker +
// weight), cue-level mode/jitter fields, and AUDITION that resolves through the REAL
// ResolveSoundCue (same weights, no-repeat state, and jitter the game uses) and plays
// through the runtime engine. Save writes the asset and nudges the validating recook.

module;
#include "Core/Prelude.h"
#include "Core/Log/Log.h"

export module editor.audio:sound_cue_page;

import foundation.core;
import foundation.content;
import foundation.runtime.client;
import foundation.audio;
import audio.pipeline;
import engine.audio;
import foundation.ui;
import editor.core;
import editor.app;

using namespace foundation::core;

export namespace editor
{
    namespace runtime = foundation::runtime;
    namespace ui = foundation::ui;
    namespace audio = foundation::audio;

    class SoundCueEditorPage final : public app::UIEditorPage
    {
    public:
        SoundCueEditorPage(EditorContext& context, runtime::IApplicationHost& host,
                           foundation::content::Instance& instance)
            : m_context(&context), m_title(instance.Name())
        {
            SetInstanceId(instance.Id());
            m_audio = host.Ctx().GetSubsystem<engine::audio::AudioSubsystem>();
            if (RefPtr<ISerializable> object = instance.ReadObject())
            {
                if (auto* asset = Cast<pipeline::SoundCueAsset>(object.Get()))
                {
                    // Field-wise copy (the Asset base is non-copyable).
                    m_asset.fileName = asset->fileName; // SourcePath copies
                    for (usize i = 0; i < pipeline::kSoundCueSlotCount; ++i)
                    {
                        m_asset.clipIds[i] = asset->clipIds[i];
                        m_asset.weights[i] = asset->weights[i];
                    }
                    m_asset.mode = asset->mode;
                    m_asset.pitchMin = asset->pitchMin;
                    m_asset.pitchMax = asset->pitchMax;
                    m_asset.volumeMin = asset->volumeMin;
                    m_asset.volumeMax = asset->volumeMax;
                }
            }

            auto column = MakeRef<ui::FlexLayout>(DefaultAllocator());
            column->Direction = ui::Orientation::Vertical;
            column->Spacing = 6.0f;

            // Page action bar (Save / Undo / Redo / Discard) at the top - the reusable page toolbar.
            m_toolbar = MakeRef<app::PageToolbar>(DefaultAllocator(), *this);
            {
                auto lp = MakeRef<ui::FlexLayoutParams>(DefaultAllocator());
                lp->Width = ui::SizeSpec::Match();
                column->AddView(m_toolbar.Get(), lp);
            }

            // Slot rows: "<clip name>" [Pick...] [Clear] weight [field]
            for (usize i = 0; i < pipeline::kSoundCueSlotCount; ++i)
            {
                auto row = MakeRef<ui::FlexLayout>(DefaultAllocator());
                row->Direction = ui::Orientation::Horizontal;
                row->Spacing = 6.0f;

                m_slotLabels[i] = MakeRef<ui::Label>(DefaultAllocator(), StringView(u8"(empty)"));
                m_slotLabels[i]->FontSize.SetValue(13.0f);
                {
                    auto lp = MakeRef<ui::FlexLayoutParams>(DefaultAllocator());
                    lp->Grow = 1.0f;
                    lp->AlignSelf = ui::Align::Center;
                    row->AddView(m_slotLabels[i].Get(), lp);
                }
                SoundCueEditorPage* self = this;
                const usize slot = i;
                auto pick = MakeRef<ui::Button>(DefaultAllocator(), StringView(u8"Pick..."));
                pick->OnClick.Add([self, slot](ui::ButtonBase*) { self->PickClip(slot); });
                row->AddView(pick.Get());
                auto clear = MakeRef<ui::Button>(DefaultAllocator(), StringView(u8"Clear"));
                clear->OnClick.Add(
                    [self, slot](ui::ButtonBase*)
                    {
                        self->m_asset.clipIds[slot] = Guid{};
                        self->RefreshSlot(slot);
                        self->MarkDirty();
                    });
                row->AddView(clear.Get());

                auto weightLabel = MakeRef<ui::Label>(DefaultAllocator(), StringView(u8"weight"));
                weightLabel->FontSize.SetValue(12.0f);
                {
                    auto lp = MakeRef<ui::FlexLayoutParams>(DefaultAllocator());
                    lp->AlignSelf = ui::Align::Center;
                    row->AddView(weightLabel.Get(), lp);
                }
                m_weightFields[i] = MakeRef<ui::NumericField>(DefaultAllocator());
                m_weightFields[i]->SetMin(0.0);
                m_weightFields[i]->SetMax(100.0);
                m_weightFields[i]->SetValue(m_asset.weights[i]);
                m_weightFields[i]->OnValueChanged.Add(
                    [self, slot](ui::NumericField*, f64 value)
                    {
                        self->m_asset.weights[slot] = static_cast<f32>(value);
                        self->MarkDirty();
                    });
                row->AddView(m_weightFields[i].Get());

                {
                    auto lp = MakeRef<ui::FlexLayoutParams>(DefaultAllocator());
                    lp->Width = ui::SizeSpec::Match();
                    column->AddView(row.Get(), lp);
                }
            }

            // Cue-level: mode + pitch/volume jitter.
            {
                auto row = MakeRef<ui::FlexLayout>(DefaultAllocator());
                row->Direction = ui::Orientation::Horizontal;
                row->Spacing = 6.0f;
                SoundCueEditorPage* self = this;
                m_modeButton = MakeRef<ui::Button>(DefaultAllocator(), StringView(u8""));
                m_modeButton->OnClick.Add(
                    [self](ui::ButtonBase*)
                    {
                        self->m_asset.mode = static_cast<u8>((self->m_asset.mode + 1) % 3);
                        self->RefreshModeButton();
                        self->MarkDirty();
                    });
                row->AddView(m_modeButton.Get());
                AddJitterField(*row, u8"pitch min", m_asset.pitchMin);
                AddJitterField(*row, u8"pitch max", m_asset.pitchMax);
                AddJitterField(*row, u8"vol min", m_asset.volumeMin);
                AddJitterField(*row, u8"vol max", m_asset.volumeMax);
                {
                    auto lp = MakeRef<ui::FlexLayoutParams>(DefaultAllocator());
                    lp->Width = ui::SizeSpec::Match();
                    column->AddView(row.Get(), lp);
                }
            }

            // Audition: resolve + play, exactly what the game does per trigger.
            {
                auto row = MakeRef<ui::FlexLayout>(DefaultAllocator());
                row->Direction = ui::Orientation::Horizontal;
                row->Spacing = 6.0f;
                SoundCueEditorPage* self = this;
                auto play = MakeRef<ui::Button>(DefaultAllocator(), StringView(u8"Audition"));
                play->OnClick.Add([self](ui::ButtonBase*) { self->Audition(); });
                row->AddView(play.Get());
                m_pauseButton = MakeRef<ui::Button>(DefaultAllocator(), StringView(u8"Pause"));
                m_pauseButton->OnClick.Add([self](ui::ButtonBase*) { self->TogglePause(); });
                row->AddView(m_pauseButton.Get());
                auto stop = MakeRef<ui::Button>(DefaultAllocator(), StringView(u8"Stop"));
                stop->OnClick.Add([self](ui::ButtonBase*) { self->StopAudition(); });
                row->AddView(stop.Get());
                m_status = MakeRef<ui::Label>(DefaultAllocator(), StringView(u8""));
                m_status->FontSize.SetValue(12.0f);
                {
                    auto lp = MakeRef<ui::FlexLayoutParams>(DefaultAllocator());
                    lp->AlignSelf = ui::Align::Center;
                    row->AddView(m_status.Get(), lp);
                }
                {
                    auto lp = MakeRef<ui::FlexLayoutParams>(DefaultAllocator());
                    lp->Width = ui::SizeSpec::Match();
                    column->AddView(row.Get(), lp);
                }
            }

            // Draft-state hint: an unauthored cue is a valid (silent) product, not an error - say so
            // clearly here instead. Shown while every slot is empty, cleared once a clip is assigned.
            m_emptyHint = MakeRef<ui::Label>(DefaultAllocator(), StringView(u8""));
            m_emptyHint->FontSize.SetValue(12.0f);
            m_emptyHint->TextColor.SetValue(Optional<Color>(Color{0.9f, 0.75f, 0.35f, 1.0f}));
            {
                auto lp = MakeRef<ui::FlexLayoutParams>(DefaultAllocator());
                lp->Width = ui::SizeSpec::Match();
                column->AddView(m_emptyHint.Get(), lp);
            }

            m_content = column;
            for (usize i = 0; i < pipeline::kSoundCueSlotCount; ++i)
            {
                RefreshSlot(i);
            }
            RefreshModeButton();
            m_savedBlob = SnapshotAsset(); // the on-load state Discard reverts to
        }

        [[nodiscard]] StringView Title() const override { return m_title.AsView(); }
        [[nodiscard]] ui::View* ContentView() override { return m_content.Get(); }

        void OnUpdate(runtime::IApplicationHost&, f32) override;

        [[nodiscard]] Status Save() override;

        // Revert to the last-saved state (the toolbar's Discard Changes) - reload the snapshot into
        // every widget without closing the page.
        void DiscardChanges() override;

        void OnClose() override;

    private:
        void AddJitterField(ui::FlexLayout& row, StringView label, f32& target);

        void PickClip(usize slot);

        void RefreshSlot(usize slot);

        // Show/hide the "empty cue - assign a clip" draft hint based on whether any slot is filled.
        void RefreshEmptyHint();

        void RefreshModeButton();

        // One REAL trigger: build a transient SoundCue from the slots (source files ->
        // in-memory clips, cached per page), resolve with the page's play state, play. Audition
        // stops any prior voice first, so repeated clicks restart rather than stack.
        void Audition();
        void StopAudition();  // stop the audition voice + reset the transport
        void TogglePause();   // pause/resume the audition voice in place (button toggles label)

        [[nodiscard]] RefPtr<audio::AudioClip> LoadSlotClip(usize slot);

        // Serialize the edited asset to a blob / restore it into the widgets. The last-saved snapshot
        // backs Discard Changes.
        [[nodiscard]] Array<byte> SnapshotAsset();
        void ApplyAssetBlob(const Array<byte>& blob);

        EditorContext* m_context = nullptr;
        engine::audio::AudioSubsystem* m_audio = nullptr;
        String m_title;
        pipeline::SoundCueAsset m_asset;
        Random m_rng;
        i32 m_lastVariant = -1;
        u32 m_sequentialCursor = 0;
        audio::VoiceHandle m_voice;
        RefPtr<ui::Button> m_pauseButton; // Pause/Resume label toggles with m_paused
        bool m_paused = false;
        String m_pickText;
        HashMap<Guid, RefPtr<audio::AudioClip>> m_clipCache;
        RefPtr<ui::View> m_content;
        RefPtr<app::PageToolbar> m_toolbar;
        Array<byte> m_savedBlob; // last-saved asset state; Discard Changes reverts to this
        RefPtr<ui::Label> m_slotLabels[pipeline::kSoundCueSlotCount];
        RefPtr<ui::NumericField> m_weightFields[pipeline::kSoundCueSlotCount];
        Array<RefPtr<ui::NumericField>> m_jitterFields;
        RefPtr<ui::Button> m_modeButton;
        RefPtr<ui::Label> m_status;
        RefPtr<ui::Label> m_emptyHint; // persistent draft-state hint (empty cue)
    };

    class SoundCuePageFactory final : public IEditorPageFactory
    {
    public:
        explicit SoundCuePageFactory(runtime::IApplicationHost& host) : m_host(&host) {}
        [[nodiscard]] const TypeInfo* PrimaryType() const override;
        [[nodiscard]] UniquePtr<EditorPage>
        CreatePage(EditorContext& context, foundation::content::Instance& instance) override;

    private:
        runtime::IApplicationHost* m_host;
    };
}
