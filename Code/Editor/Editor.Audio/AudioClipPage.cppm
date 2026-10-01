// SPDX-License-Identifier: MIT
// Copyright (c) 2026-Present Robert Campbell

// Editor::Audio - the `editor.audio` module.
//
// AudioClipPage: the audition page. Opens an AudioClipAsset with a peak
// waveform (decoded from the copied source file - the same container bytes the cook
// writes through) and Play/Stop that audition through the RUNTIME CONTEXT's
// AudioSubsystem - the GAME's engine, buses, and voice pool, so what you hear IS what
// the game plays (the "cook says fine, ears say nothing" gap this page closes).
// Auditioning honors the asset's loop intent; a playhead tracks the voice.

module;
#include "Core/Prelude.h"
#include "Core/Log/Log.h"

export module editor.audio;

export import :sound_cue_page;
export import :bus_layout_page;
export import :thumbnail_generator;

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

    /// The peak waveform strip: symmetric per-bucket bars around the midline plus an
    /// optional playhead (fraction of the clip; < 0 hides it).
    class WaveformView final : public ui::View
    {
    public:
        void SetPeaks(Array<f32> peaks);
        void SetPlayheadFraction(f32 fraction);

        void OnDraw(ui::UIDrawContext& ctx) override;

    private:
        Array<f32> m_peaks;
        f32 m_playhead = -1.0f;
    };

    class AudioClipEditorPage final : public app::UIEditorPage, public IPlaybackPage
    {
    public:
        AudioClipEditorPage(EditorContext& context, runtime::IApplicationHost& host,
                            foundation::content::Instance& instance)
            : app::UIEditorPage(context.Allocator()),
              m_context(&context), m_title(instance.Name())
        {
            SetInstanceId(instance.Id());
            Provide<IPlaybackPage>(*this);
            m_audio = host.Ctx().GetSubsystem<engine::audio::AudioSubsystem>();
            LoadClip(instance);

            auto column = MakeRef<ui::FlexLayout>(Allocator());
            column->Direction = ui::Orientation::Vertical;
            column->Spacing = 8.0f;

            m_info = MakeRef<ui::Label>(Allocator(), StringView(u8""));
            m_info->FontSize.SetValue(13.0f);
            {
                ui::LayoutStyle lp;
                lp.Width = ui::SizeSpec::Match();
                column->AddView(m_info.Get(), lp);
            }

            m_waveform = MakeRef<WaveformView>(Allocator());
            {
                ui::LayoutStyle lp;
                lp.Width = ui::SizeSpec::Match();
                lp.Height = ui::SizeSpec::Fixed(ui::Unit::Dp(160));
                column->AddView(m_waveform.Get(), lp);
            }

            // The page toolbar: the playback transport, the audition volume and the status.
            AudioClipEditorPage* self = this;
            m_toolbar = MakeRef<app::PageToolbar>(Allocator(), *this, m_context->Actions(),
                                                  app::PageToolbar::Standard::None);
            m_toolbar->AddPlayback();
            m_toolbar->AddSeparator();
            (void)m_toolbar->AddLabel(u8"Vol");
            // Audition-local gain (not persisted - it only scales THIS page's preview voice).
            m_volumeSlider = MakeRef<ui::Slider>(Allocator());
            m_volumeSlider->Min.SetValue(0.0f);
            m_volumeSlider->Max.SetValue(1.0f);
            m_volumeSlider->Step.SetValue(0.05f);
            m_volumeSlider->Value.SetValue(1.0f);
            m_volumeSlider->OnValueChanged.Add(ui::Event<void(ui::Slider*, f32)>::Handler{
                [self](ui::Slider*, f32 v) { self->SetAuditionVolume(v); }});
            {
                ui::LayoutStyle lp;
                lp.Width = ui::SizeSpec::Fixed(ui::Unit::Dp(90));
                lp.AlignSelf = ui::Align::Center;
                m_toolbar->AddItem(m_volumeSlider.Get(), lp);
            }
            m_status = RefPtr<ui::Label>(m_toolbar->AddLabel(u8""));
            {
                ui::LayoutStyle lp;
                lp.Width = ui::SizeSpec::Match();
                column->InsertView(m_toolbar.Get(), 0, lp);
            }

            m_content = column;
            RefreshInfo();
        }

        [[nodiscard]] StringView Title() const override { return m_title.AsView(); }
        [[nodiscard]] ui::View* ContentView() override { return m_content.Get(); }
        [[nodiscard]] Status Save() override;

        void OnUpdate(runtime::IApplicationHost&, f32) override;

        void OnClose() override { StopAudition(); }

        // ---- IPlaybackPage ----
        [[nodiscard]] bool CanPlay() const override { return m_clip.Get() != nullptr; }
        [[nodiscard]] bool IsPlaying() const override { return m_voice.IsValid() && !m_paused; }
        void Play() override;
        void Pause() override;
        void Stop() override { StopAudition(); }
        void Restart() override { Audition(); }

    private:
        void LoadClip(foundation::content::Instance& instance);

        void RefreshInfo();

        void Audition();

        void StopAudition();

        // Pause/resume the audition voice in place; audition-local gain.
        void TogglePause();
        void SetAuditionVolume(f32 volume);

        EditorContext* m_context = nullptr;
        engine::audio::AudioSubsystem* m_audio = nullptr; // the RUNTIME context's subsystem
        String m_title;
        bool m_loop = false;
        bool m_paused = false;
        f32 m_auditionVolume = 1.0f;
        RefPtr<audio::AudioClip> m_clip;
        audio::VoiceHandle m_voice;
        RefPtr<ui::View> m_content;
        RefPtr<ui::Label> m_info;
        RefPtr<ui::Label> m_status;
        RefPtr<app::PageToolbar> m_toolbar;
        RefPtr<ui::Slider> m_volumeSlider;
        RefPtr<WaveformView> m_waveform;
    };

    class AudioClipPageFactory final : public IEditorPageFactory
    {
    public:
        explicit AudioClipPageFactory(runtime::IApplicationHost& host) : m_host(&host) {}
        [[nodiscard]] const TypeInfo* PrimaryType() const override;
        [[nodiscard]] UniquePtr<EditorPage>
        CreatePage(EditorContext& context, foundation::content::Instance& instance) override;

    private:
        runtime::IApplicationHost* m_host;
    };

    inline void RegisterAudioClipEditor(EditorContext& context, runtime::IApplicationHost& host)
    {
        context.Pages().Register(UniquePtr<IEditorPageFactory>(
            editor::EditorRootAllocator().New<AudioClipPageFactory>(host), editor::EditorRootAllocator()));
        context.Pages().Register(UniquePtr<IEditorPageFactory>(
            editor::EditorRootAllocator().New<SoundCuePageFactory>(host), editor::EditorRootAllocator()));
        if (context.Thumbnails() != nullptr)
        {
            RegisterAudioThumbnailGenerator(*context.Thumbnails());
        }
    }
}
