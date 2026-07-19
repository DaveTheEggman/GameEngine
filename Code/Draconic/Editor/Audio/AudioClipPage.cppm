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

import draconic.core;
import draconic.content;
import draconic.runtime.client;
import draconic.audio;
import draconic.audio.editor;
import draconic.audio.subsystem;
import draconic.ui;
import draconic.editor.core;
import draconic.editor.app;

using namespace draconic::core;

export namespace draconic::editor
{
    namespace grt = draconic::runtime;
    namespace gui = draconic::ui;
    namespace gaudio = draconic::audio;

    /// The peak waveform strip: symmetric per-bucket bars around the midline plus an
    /// optional playhead (fraction of the clip; < 0 hides it).
    class WaveformView final : public gui::View
    {
    public:
        void SetPeaks(Array<f32> peaks)
        {
            m_peaks = Move(peaks);
            Invalidate();
        }
        void SetPlayheadFraction(f32 fraction)
        {
            if (m_playhead != fraction)
            {
                m_playhead = fraction;
                Invalidate();
            }
        }

        void OnDraw(gui::UIDrawContext& ctx) override
        {
            const Rectangle bounds{ 0.0f, 0.0f, Width(), Height() };
            ctx.VG().FillRect(bounds, Color{ 0.10f, 0.11f, 0.13f, 1.0f });
            if (m_peaks.IsEmpty() || bounds.width <= 2.0f || bounds.height <= 2.0f)
            {
                return;
            }
            const f32 mid = bounds.height * 0.5f;
            const f32 barWidth = bounds.width / static_cast<f32>(m_peaks.Size());
            const Color barColor{ 64.0f / 255.0f, 200.0f / 255.0f, 190.0f / 255.0f, 0.9f };
            for (usize i = 0; i < m_peaks.Size(); ++i)
            {
                const f32 half = Max(1.0f, m_peaks[i] * (mid - 2.0f));
                ctx.VG().FillRect(Rectangle{ static_cast<f32>(i) * barWidth, mid - half,
                                             Max(1.0f, barWidth - 1.0f), half * 2.0f },
                                  barColor);
            }
            if (m_playhead >= 0.0f && m_playhead <= 1.0f)
            {
                ctx.VG().FillRect(Rectangle{ m_playhead * bounds.width - 1.0f, 0.0f,
                                             2.0f, bounds.height },
                                  Color{ 0.95f, 0.85f, 0.4f, 1.0f });
            }
        }

    private:
        Array<f32> m_peaks;
        f32 m_playhead = -1.0f;
    };

    class AudioClipEditorPage final : public app::UIEditorPage
    {
    public:
        AudioClipEditorPage(EditorContext& context, grt::IApplicationHost& host,
                            draconic::content::Instance& instance)
            : m_context(&context), m_title(instance.Name())
        {
            SetInstanceId(instance.Id());
            m_audio = host.Ctx().GetSubsystem<gaudio::AudioSubsystem>();
            LoadClip(instance);

            auto column = MakeRef<gui::FlexLayout>(DefaultAllocator());
            column->Direction = gui::Orientation::Vertical;
            column->Spacing = 8.0f;

            m_info = MakeRef<gui::Label>(DefaultAllocator(), StringView(u8""));
            m_info->FontSize.SetValue(13.0f);
            {
                auto lp = MakeRef<gui::FlexLayoutParams>(DefaultAllocator());
                lp->Width = gui::SizeSpec::Match();
                column->AddView(m_info.Get(), lp);
            }

            m_waveform = MakeRef<WaveformView>(DefaultAllocator());
            {
                auto lp = MakeRef<gui::FlexLayoutParams>(DefaultAllocator());
                lp->Width = gui::SizeSpec::Match();
                lp->Height = gui::SizeSpec::Fixed(gui::Unit::Px(160));
                column->AddView(m_waveform.Get(), lp);
            }

            auto controls = MakeRef<gui::FlexLayout>(DefaultAllocator());
            controls->Direction = gui::Orientation::Horizontal;
            controls->Spacing = 8.0f;
            AudioClipEditorPage* self = this;
            m_playButton = MakeRef<gui::Button>(DefaultAllocator(), StringView(u8"Play"));
            m_playButton->OnClick.Add([self](gui::ButtonBase*) { self->Audition(); });
            controls->AddView(m_playButton.Get());
            m_stopButton = MakeRef<gui::Button>(DefaultAllocator(), StringView(u8"Stop"));
            m_stopButton->OnClick.Add([self](gui::ButtonBase*) { self->StopAudition(); });
            controls->AddView(m_stopButton.Get());
            m_status = MakeRef<gui::Label>(DefaultAllocator(), StringView(u8""));
            m_status->FontSize.SetValue(12.0f);
            controls->AddView(m_status.Get());
            {
                auto lp = MakeRef<gui::FlexLayoutParams>(DefaultAllocator());
                lp->Width = gui::SizeSpec::Match();
                column->AddView(controls.Get(), lp);
            }

            m_content = column;
            RefreshInfo();
        }

        [[nodiscard]] StringView Title() const override { return m_title.AsView(); }
        [[nodiscard]] gui::View* ContentView() override { return m_content.Get(); }
        [[nodiscard]] Status Save() override { return Status{}; }   // audition-only (options edit via inspector)

        void OnUpdate(grt::IApplicationHost&, f32 deltaTime) override
        {
            // Playhead: elapsed time against the clip duration (looping wraps). The
            // voice handle going invalid (finished/stolen) parks the head.
            if (!m_voice.IsValid() || m_audio == nullptr || m_audio->Engine() == nullptr)
            {
                return;
            }
            if (!m_audio->Engine()->IsPlaying(m_voice))
            {
                StopAudition();
                return;
            }
            m_elapsed += deltaTime;
            const f32 duration = m_clip.Get() != nullptr ? m_clip->durationSeconds : 0.0f;
            if (duration <= 0.0f) { return; }
            f32 fraction = m_elapsed / duration;
            if (m_loop) { fraction = fraction - static_cast<f32>(static_cast<i64>(fraction)); }
            m_waveform->SetPlayheadFraction(Min(fraction, 1.0f));
        }

        void OnClose() override { StopAudition(); }

    private:
        void LoadClip(draconic::content::Instance& instance)
        {
            RefPtr<ISerializable> object = instance.ReadObject();
            auto* asset = Cast<gaudio::AudioClipAsset>(object.Get());
            if (asset == nullptr || m_context->Project() == nullptr) { return; }
            m_loop = asset->loop;
            const String path = PathJoin(m_context->Project()->SourcesRoot().AsView(),
                                         asset->fileName.AsView());
            Result<Array<byte>> bytes = ReadFile(path.AsView());
            if (!bytes.HasValue())
            {
                DRACONIC_LOG_WARNING(u8"Editor", u8"audio source missing: {}", path);
                return;
            }
            gaudio::AudioClipMetadata metadata;
            if (!gaudio::ProbeAudioClipMetadata(
                    Span<const byte>(bytes.Value().Data(), bytes.Value().Size()), metadata))
            {
                return;
            }
            m_clip = MakeRef<gaudio::AudioClip>(DefaultAllocator());
            m_clip->channels = metadata.channels;
            m_clip->sampleRate = metadata.sampleRate;
            m_clip->frameCount = metadata.frameCount;
            m_clip->durationSeconds = metadata.durationSeconds;
            m_clip->gain = asset->gain;
            m_clip->loop = asset->loop;
            m_clip->loopStartFrame = asset->loopStartFrame;
            m_clip->loopEndFrame = asset->loopEndFrame;
            m_clip->encodedData = Move(bytes.Value());
        }

        void RefreshInfo()
        {
            if (m_clip.Get() == nullptr)
            {
                m_info->SetText(u8"Source file missing or undecodable.");
                m_playButton->IsEnabled = false;
                m_stopButton->IsEnabled = false;
                return;
            }
            String text = Format(u8"{} ch  |  {} Hz  |  {} s{}", m_clip->channels,
                                 m_clip->sampleRate, FormatFixed(m_clip->durationSeconds, 2),
                                 m_loop ? StringView(u8"  |  loops") : StringView(u8""));
            m_info->SetText(text.AsView());
            Array<f32> peaks;
            if (gaudio::BuildWaveformPeaks(
                    Span<const byte>(m_clip->encodedData.Data(), m_clip->encodedData.Size()),
                    256, peaks))
            {
                m_waveform->SetPeaks(Move(peaks));
            }
        }

        void Audition()
        {
            if (m_clip.Get() == nullptr || m_audio == nullptr || m_audio->Engine() == nullptr)
            {
                return;
            }
            StopAudition();
            gaudio::AudioPlayParams params;
            params.loop = m_loop;
            params.allowDedupe = false;   // rapid re-audition must restart, never merge
            m_voice = m_audio->Engine()->Play(m_clip, params);
            m_elapsed = 0.0f;
            m_status->SetText(m_voice.IsValid() ? StringView(u8"Playing...")
                                                : StringView(u8"No voice (engine headless?)"));
        }

        void StopAudition()
        {
            if (m_audio != nullptr && m_audio->Engine() != nullptr && m_voice.IsValid())
            {
                m_audio->Engine()->Stop(m_voice);
            }
            m_voice = gaudio::VoiceHandle{};
            m_elapsed = 0.0f;
            m_waveform->SetPlayheadFraction(-1.0f);
            m_status->SetText(u8"");
        }

        EditorContext* m_context = nullptr;
        gaudio::AudioSubsystem* m_audio = nullptr;   // the RUNTIME context's subsystem
        String m_title;
        bool m_loop = false;
        RefPtr<gaudio::AudioClip> m_clip;
        gaudio::VoiceHandle m_voice;
        f32 m_elapsed = 0.0f;
        RefPtr<gui::View> m_content;
        RefPtr<gui::Label> m_info;
        RefPtr<gui::Label> m_status;
        RefPtr<gui::Button> m_playButton;
        RefPtr<gui::Button> m_stopButton;
        RefPtr<WaveformView> m_waveform;
    };

    class AudioClipPageFactory final : public IEditorPageFactory
    {
    public:
        explicit AudioClipPageFactory(grt::IApplicationHost& host) : m_host(&host) {}
        [[nodiscard]] const TypeInfo* PrimaryType() const override
        {
            return &gaudio::AudioClipAsset::StaticType();
        }
        [[nodiscard]] UniquePtr<EditorPage> CreatePage(EditorContext& context,
                                                       draconic::content::Instance& instance) override
        {
            auto* page = DefaultAllocator().New<AudioClipEditorPage>(context, *m_host, instance);
            return UniquePtr<EditorPage>(page, DefaultAllocator());
        }

    private:
        grt::IApplicationHost* m_host;
    };

    inline void RegisterAudioClipEditor(EditorContext& context, grt::IApplicationHost& host)
    {
        context.Pages().Register(UniquePtr<IEditorPageFactory>(
            DefaultAllocator().New<AudioClipPageFactory>(host), DefaultAllocator()));
    }
}
