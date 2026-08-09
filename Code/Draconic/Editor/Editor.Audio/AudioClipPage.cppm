// Draconic::EditorAudio - the `draconic.editor.audio` module.
//
// AudioClipPage (audio.md P2): the audition page. Opens an AudioClipAsset with a peak
// waveform (decoded from the copied source file - the same container bytes the cook
// writes through) and Play/Stop that audition through the RUNTIME CONTEXT's
// AudioSubsystem - the GAME's engine, buses, and voice pool, so what you hear IS what
// the game plays (the "cook says fine, ears say nothing" gap this page closes).
// Auditioning honors the asset's loop intent; a playhead tracks the voice.

module;
#include "Core/Prelude.h"
#include "Core/Log/Log.h"

export module draconic.editor.audio;

export import :sound_cue_page;
export import :bus_layout_page;

import draconic.core;
import draconic.content;
import draconic.runtime.client;
import draconic.audio;
import draconic.audio.pipeline;
import draconic.engine.audio;
import draconic.ui;
import draconic.editor.core;
import draconic.editor.app;

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

    class AudioClipEditorPage final : public app::UIEditorPage
    {
    public:
        AudioClipEditorPage(EditorContext& context, runtime::IApplicationHost& host,
                            foundation::content::Instance& instance)
            : m_context(&context), m_title(instance.Name())
        {
            SetInstanceId(instance.Id());
            m_audio = host.Ctx().GetSubsystem<engine::audio::AudioSubsystem>();
            LoadClip(instance);

            auto column = MakeRef<ui::FlexLayout>(DefaultAllocator());
            column->Direction = ui::Orientation::Vertical;
            column->Spacing = 8.0f;

            m_info = MakeRef<ui::Label>(DefaultAllocator(), StringView(u8""));
            m_info->FontSize.SetValue(13.0f);
            {
                auto lp = MakeRef<ui::FlexLayoutParams>(DefaultAllocator());
                lp->Width = ui::SizeSpec::Match();
                column->AddView(m_info.Get(), lp);
            }

            m_waveform = MakeRef<WaveformView>(DefaultAllocator());
            {
                auto lp = MakeRef<ui::FlexLayoutParams>(DefaultAllocator());
                lp->Width = ui::SizeSpec::Match();
                lp->Height = ui::SizeSpec::Fixed(ui::Unit::Px(160));
                column->AddView(m_waveform.Get(), lp);
            }

            auto controls = MakeRef<ui::FlexLayout>(DefaultAllocator());
            controls->Direction = ui::Orientation::Horizontal;
            controls->Spacing = 8.0f;
            AudioClipEditorPage* self = this;
            m_playButton = MakeRef<ui::Button>(DefaultAllocator(), StringView(u8"Play"));
            m_playButton->OnClick.Add([self](ui::ButtonBase*) { self->Audition(); });
            controls->AddView(m_playButton.Get());
            m_pauseButton = MakeRef<ui::Button>(DefaultAllocator(), StringView(u8"Pause"));
            m_pauseButton->OnClick.Add([self](ui::ButtonBase*) { self->TogglePause(); });
            controls->AddView(m_pauseButton.Get());
            m_stopButton = MakeRef<ui::Button>(DefaultAllocator(), StringView(u8"Stop"));
            m_stopButton->OnClick.Add([self](ui::ButtonBase*) { self->StopAudition(); });
            controls->AddView(m_stopButton.Get());

            // Audition-local gain (not persisted - it only scales THIS page's preview voice).
            auto volLabel = MakeRef<ui::Label>(DefaultAllocator(), StringView(u8"Vol"));
            volLabel->FontSize.SetValue(12.0f);
            controls->AddView(volLabel.Get());
            m_volumeSlider = MakeRef<ui::Slider>(DefaultAllocator());
            m_volumeSlider->Min.SetValue(0.0f);
            m_volumeSlider->Max.SetValue(1.0f);
            m_volumeSlider->Step.SetValue(0.05f);
            m_volumeSlider->Value.SetValue(1.0f);
            m_volumeSlider->OnValueChanged.Add(ui::Event<void(ui::Slider*, f32)>::Handler{
                [self](ui::Slider*, f32 v) { self->SetAuditionVolume(v); }});
            {
                auto lp = MakeRef<ui::FlexLayoutParams>(DefaultAllocator());
                lp->Width = ui::SizeSpec::Fixed(ui::Unit::Px(90));
                lp->AlignSelf = ui::Align::Center;
                controls->AddView(m_volumeSlider.Get(), lp);
            }

            m_status = MakeRef<ui::Label>(DefaultAllocator(), StringView(u8""));
            m_status->FontSize.SetValue(12.0f);
            controls->AddView(m_status.Get());
            {
                auto lp = MakeRef<ui::FlexLayoutParams>(DefaultAllocator());
                lp->Width = ui::SizeSpec::Match();
                column->AddView(controls.Get(), lp);
            }

            m_content = column;
            RefreshInfo();
        }

        [[nodiscard]] StringView Title() const override { return m_title.AsView(); }
        [[nodiscard]] ui::View* ContentView() override { return m_content.Get(); }
        [[nodiscard]] Status Save() override;

        void OnUpdate(runtime::IApplicationHost&, f32) override;

        void OnClose() override { StopAudition(); }

    private:
        void LoadClip(foundation::content::Instance& instance);

        void RefreshInfo();

        void Audition();

        void StopAudition();

        // Pause/resume the audition voice in place (button toggles its label); audition-local gain.
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
        RefPtr<ui::Button> m_playButton;
        RefPtr<ui::Button> m_pauseButton;
        RefPtr<ui::Button> m_stopButton;
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
            DefaultAllocator().New<AudioClipPageFactory>(host), DefaultAllocator()));
        context.Pages().Register(UniquePtr<IEditorPageFactory>(
            DefaultAllocator().New<SoundCuePageFactory>(host), DefaultAllocator()));
    }
}
