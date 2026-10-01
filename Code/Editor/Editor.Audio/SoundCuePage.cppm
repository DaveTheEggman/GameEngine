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

    class SoundCueEditorPage final : public app::UIEditorPage, public IPlaybackPage
    {
        class EditSoundCueCommand final : public IEditorCommand
        {
        public:
            EditSoundCueCommand(SoundCueEditorPage& page, StringView mergeKey, Array<byte> before,
                                Array<byte> after)
                : m_page(&page), m_mergeKey(mergeKey), m_before(Move(before)), m_after(Move(after))
            {
            }
            [[nodiscard]] bool Execute() override
            {
                m_page->ApplyAssetBlob(m_after);
                return true;
            }
            void Undo() override { m_page->ApplyAssetBlob(m_before); }
            [[nodiscard]] StringView TypeId() const override { return u8"edit_soundcue"; }
            [[nodiscard]] bool MergeInto(IEditorCommand& previous) override
            {
                auto& prev = static_cast<EditSoundCueCommand&>(previous);
                // Empty key = a discrete op (pick/clear/mode): never coalesce those.
                if (prev.m_page != m_page || m_mergeKey.IsEmpty() ||
                    prev.m_mergeKey.AsView() != m_mergeKey.AsView())
                {
                    return false;
                }
                prev.m_after = Move(m_after);
                return true;
            }

        private:
            SoundCueEditorPage* m_page;
            String m_mergeKey;
            Array<byte> m_before;
            Array<byte> m_after;
        };

    public:
        SoundCueEditorPage(EditorContext& context, runtime::IApplicationHost& host,
                           foundation::content::Instance& instance)
            : app::UIEditorPage(context.Allocator()),
              m_context(&context), m_title(instance.Name())
        {
            SetInstanceId(instance.Id());
            Provide<IPlaybackPage>(*this);
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

            auto column = MakeRef<ui::FlexLayout>(Allocator());
            column->Direction = ui::Orientation::Vertical;
            column->Spacing = 6.0f;

            // Page action bar (Save / Undo / Redo / Discard) at the top - the reusable page toolbar.
            m_toolbar = MakeRef<app::PageToolbar>(Allocator(), *this, m_context->Actions());
            // The audition is the playback transport: it resolves a variant as the runtime does.
            m_toolbar->AddPlayback();
            m_status = RefPtr<ui::Label>(m_toolbar->AddLabel(u8""));
            {
                ui::LayoutStyle lp;
                lp.Width = ui::SizeSpec::Match();
                column->AddView(m_toolbar.Get(), lp);
            }

            // Slot rows: the clip's asset slot (pick, drop and clear are one assignment), then
            // its weight.
            for (usize i = 0; i < pipeline::kSoundCueSlotCount; ++i)
            {
                auto row = MakeRef<ui::FlexLayout>(Allocator());
                row->Direction = ui::Orientation::Horizontal;
                row->Spacing = 6.0f;

                SoundCueEditorPage* self = this;
                const usize slot = i;
                const StringView clipTypes[] = {u8"AudioClipAsset"};
                m_slotRows[i] = MakeRef<app::ResourceRefEditor>(
                    Allocator(), StringView(u8"Clip"), StringView(u8"(empty)"), StringView{},
                    Span<const StringView>{clipTypes, 1});
                m_slotRows[i]->SetEmptyText(u8"(empty)");
                m_slotRows[i]->BindAsset(*m_context,
                                         [self, slot]() { return self->m_asset.clipIds[slot]; },
                                         [self, slot](const Guid& id)
                                         {
                                             self->m_asset.clipIds[slot] = id;
                                             self->RefreshEmptyHint();
                                             self->CommitEdit(u8"");
                                         });
                {
                    ui::LayoutStyle lp;
                    lp.FlexGrow = 1.0f;
                    lp.AlignSelf = ui::Align::Center;
                    row->AddView(m_slotRows[i]->EditorView(), lp);
                }

                auto weightLabel = MakeRef<ui::Label>(Allocator(), StringView(u8"weight"));
                weightLabel->FontSize.SetValue(12.0f);
                {
                    ui::LayoutStyle lp;
                    lp.AlignSelf = ui::Align::Center;
                    row->AddView(weightLabel.Get(), lp);
                }
                m_weightFields[i] = MakeRef<ui::NumericField>(Allocator());
                m_weightFields[i]->SetMin(0.0);
                m_weightFields[i]->SetMax(100.0);
                m_weightFields[i]->SetValue(m_asset.weights[i]);
                m_weightFields[i]->OnValueChanged.Add(
                    [self, slot](ui::NumericField*, f64 value)
                    {
                        self->m_asset.weights[slot] = static_cast<f32>(value);
                        // Keyed: a scrub coalesces into ONE undo step per field.
                        u8 key[] = {'w', 'e', 'i', 'g', 'h', 't',
                                    static_cast<u8>('0' + slot), 0};
                        self->CommitEdit(StringView(reinterpret_cast<const char8_t*>(key)));
                    });
                row->AddView(m_weightFields[i].Get());

                {
                    ui::LayoutStyle lp;
                    lp.Width = ui::SizeSpec::Match();
                    column->AddView(row.Get(), lp);
                }
            }

            // Cue-level: mode + pitch/volume jitter.
            {
                auto row = MakeRef<ui::FlexLayout>(Allocator());
                row->Direction = ui::Orientation::Horizontal;
                row->Spacing = 6.0f;
                SoundCueEditorPage* self = this;
                m_modeButton = MakeRef<ui::Button>(Allocator(), StringView(u8""));
                m_modeButton->OnClick.Add(
                    [self](ui::ButtonBase*)
                    {
                        self->m_asset.mode = static_cast<u8>((self->m_asset.mode + 1) % 3);
                        self->RefreshModeButton();
                        self->CommitEdit(u8"");
                    });
                row->AddView(m_modeButton.Get());
                AddJitterField(*row, u8"pitch min", m_asset.pitchMin);
                AddJitterField(*row, u8"pitch max", m_asset.pitchMax);
                AddJitterField(*row, u8"vol min", m_asset.volumeMin);
                AddJitterField(*row, u8"vol max", m_asset.volumeMax);
                {
                    ui::LayoutStyle lp;
                    lp.Width = ui::SizeSpec::Match();
                    column->AddView(row.Get(), lp);
                }
            }

            // Draft-state hint: an unauthored cue is a valid (silent) product, not an error - say so
            // clearly here instead. Shown while every slot is empty, cleared once a clip is assigned.
            m_emptyHint = MakeRef<ui::Label>(Allocator(), StringView(u8""));
            m_emptyHint->FontSize.SetValue(12.0f);
            m_emptyHint->TextColor.SetValue(Optional<Color>(Color{0.9f, 0.75f, 0.35f, 1.0f}));
            {
                ui::LayoutStyle lp;
                lp.Width = ui::SizeSpec::Match();
                column->AddView(m_emptyHint.Get(), lp);
            }

            m_content = column;
            for (usize i = 0; i < pipeline::kSoundCueSlotCount; ++i)
            {
                RefreshSlot(i);
            }
            RefreshModeButton();
            m_savedBlob = SnapshotAsset(); // the on-load state Discard reverts to
            m_undoBaseline = m_savedBlob;   // and the first edit command's "before"
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

        void RefreshSlot(usize slot);

        // Show/hide the "empty cue - assign a clip" draft hint based on whether any slot is filled.
        void RefreshEmptyHint();

        void RefreshModeButton();

        // One REAL trigger: build a transient SoundCue from the slots (source files ->
        // in-memory clips, cached per page), resolve with the page's play state, play. Audition
        // stops any prior voice first, so repeated clicks restart rather than stack.
        void Audition();
        void StopAudition();  // stop the audition voice + reset the transport
        void TogglePause();   // pause/resume the audition voice in place

    public:
        // ---- IPlaybackPage: an audition resolves a variant as the runtime would ----
        [[nodiscard]] bool CanPlay() const override;
        [[nodiscard]] bool IsPlaying() const override { return m_voice.IsValid() && !m_paused; }
        void Play() override;
        void Pause() override;
        void Stop() override { StopAudition(); }
        void Restart() override { Audition(); }

    private:

        [[nodiscard]] RefPtr<audio::AudioClip> LoadSlotClip(usize slot);

        // Serialize the edited asset to a blob / restore it into the widgets. The last-saved snapshot
        // backs Discard Changes.
        [[nodiscard]] Array<byte> SnapshotAsset();
        void ApplyAssetBlob(const Array<byte>& blob);
        /// Push one undo step: snapshot after the mutation, Execute a blob command against the
        /// baseline. Keyed commits coalesce (field scrubs); an empty key never merges.
        void CommitEdit(StringView mergeKey);

        EditorContext* m_context = nullptr;
        engine::audio::AudioSubsystem* m_audio = nullptr;
        String m_title;
        pipeline::SoundCueAsset m_asset;
        Random m_rng;
        i32 m_lastVariant = -1;
        u32 m_sequentialCursor = 0;
        audio::VoiceHandle m_voice;
        bool m_paused = false;
        String m_pickText;
        HashMap<Guid, RefPtr<audio::AudioClip>> m_clipCache;
        RefPtr<ui::View> m_content;
        RefPtr<app::PageToolbar> m_toolbar;
        Array<byte> m_savedBlob; // last-saved asset state; Discard Changes reverts to this
        Array<byte> m_undoBaseline; // the "before" of the NEXT edit command
        RefPtr<app::ResourceRefEditor> m_slotRows[pipeline::kSoundCueSlotCount]; // clip slots
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
