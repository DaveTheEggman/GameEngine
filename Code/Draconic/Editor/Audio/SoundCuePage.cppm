// Draconic::EditorAudio - the `:sound_cue_page` partition.
//
// SoundCuePage (audio.md P3): the cue editor - eight variant slot rows (clip picker +
// weight), cue-level mode/jitter fields, and AUDITION that resolves through the REAL
// ResolveSoundCue (same weights, no-repeat state, and jitter the game uses) and plays
// through the runtime engine. Save writes the asset and nudges the validating recook.

module;
#include "Core/Prelude.h"
#include "Core/Log/Log.h"

export module draconic.editor.audio:sound_cue_page;

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
    namespace runtime = draconic::runtime;
    namespace ui = draconic::ui;
    namespace audio = draconic::audio;

    class SoundCueEditorPage final : public app::UIEditorPage
    {
    public:
        SoundCueEditorPage(EditorContext& context, runtime::IApplicationHost& host,
                           draconic::content::Instance& instance)
            : m_context(&context), m_title(instance.Name())
        {
            SetInstanceId(instance.Id());
            m_audio = host.Ctx().GetSubsystem<audio::AudioSubsystem>();
            if (RefPtr<ISerializable> object = instance.ReadObject())
            {
                if (auto* asset = Cast<audio::SoundCueAsset>(object.Get()))
                {
                    // Field-wise copy (the Asset base is non-copyable).
                    m_asset.fileName = String(asset->fileName.AsView());
                    for (usize i = 0; i < audio::kSoundCueSlotCount; ++i)
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

            // Slot rows: "<clip name>" [Pick...] [Clear] weight [field]
            for (usize i = 0; i < audio::kSoundCueSlotCount; ++i)
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
                clear->OnClick.Add([self, slot](ui::ButtonBase*) {
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
                m_weightFields[i]->OnValueChanged.Add([self, slot](ui::NumericField*, f64 value) {
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
                m_modeButton->OnClick.Add([self](ui::ButtonBase*) {
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

            m_content = column;
            for (usize i = 0; i < audio::kSoundCueSlotCount; ++i) { RefreshSlot(i); }
            RefreshModeButton();
        }

        [[nodiscard]] StringView Title() const override { return m_title.AsView(); }
        [[nodiscard]] ui::View* ContentView() override { return m_content.Get(); }

        void OnUpdate(runtime::IApplicationHost&, f32) override
        {
            // Status line: append the auditioning voice's TRUE cursor (item: voice-
            // cursor playhead) while it plays; restore the plain pick line after.
            if (m_audio == nullptr || m_audio->Engine() == nullptr || !m_voice.IsValid())
            {
                return;
            }
            audio::VoiceStatus status;
            if (m_audio->Engine()->GetVoiceStatus(m_voice, status) && status.playing)
            {
                String text = Format(u8"{}  |  {} s", m_pickText,
                                     FormatFixed(status.cursorSeconds, 1));
                m_status->SetText(text.AsView());
            }
            else
            {
                m_voice = audio::VoiceHandle{};
                m_status->SetText(m_pickText.AsView());
            }
        }

        [[nodiscard]] Status Save() override
        {
            draconic::content::Instance* instance =
                (m_context->Project() != nullptr)
                    ? m_context->Project()->SourceDb().GetInstance(InstanceId()) : nullptr;
            if (instance == nullptr) { return Status{ ErrorCode::NotFound }; }
            const Status written = instance->WriteObject(m_asset);
            if (written.IsOk())
            {
                ClearDirty();
                if (m_context->OnCookRequested) { m_context->OnCookRequested(false); }
            }
            return written;
        }

        void OnClose() override
        {
            if (m_audio != nullptr && m_audio->Engine() != nullptr && m_voice.IsValid())
            {
                m_audio->Engine()->Stop(m_voice);
            }
        }

    private:
        void AddJitterField(ui::FlexLayout& row, StringView label, f32& target)
        {
            auto text = MakeRef<ui::Label>(DefaultAllocator(), label);
            text->FontSize.SetValue(12.0f);
            {
                auto lp = MakeRef<ui::FlexLayoutParams>(DefaultAllocator());
                lp->AlignSelf = ui::Align::Center;
                row.AddView(text.Get(), lp);
            }
            auto field = MakeRef<ui::NumericField>(DefaultAllocator());
            field->SetMin(0.0);
            field->SetMax(4.0);
            field->SetDecimalPlaces(2);
            field->SetValue(target);
            SoundCueEditorPage* self = this;
            f32* slot = &target;   // points into m_asset (stable for the page's lifetime)
            field->OnValueChanged.Add([self, slot](ui::NumericField*, f64 value) {
                *slot = static_cast<f32>(value);
                self->MarkDirty();
            });
            row.AddView(field.Get());
            m_jitterFields.PushBack(field);
        }

        void PickClip(usize slot)
        {
            ui::UIContext* uiContext = m_content.Get() != nullptr ? m_content->Context : nullptr;
            if (uiContext == nullptr) { return; }
            Array<String> typeNames;
            typeNames.PushBack(String(u8"AudioClipAsset"));
            auto picker = MakeRef<app::AssetPickerDialog>(DefaultAllocator(), *m_context,
                                                          Move(typeNames));
            SoundCueEditorPage* self = this;
            picker->OnPicked = [self, slot](const Guid& id) {
                self->m_asset.clipIds[slot] = id;
                self->RefreshSlot(slot);
                self->MarkDirty();
            };
            picker->Show(uiContext);
        }

        void RefreshSlot(usize slot)
        {
            const Guid& id = m_asset.clipIds[slot];
            if (id.IsNil() || m_context->Project() == nullptr)
            {
                m_slotLabels[slot]->SetText(u8"(empty)");
                return;
            }
            draconic::content::Instance* clip = m_context->Project()->SourceDb().GetInstance(id);
            m_slotLabels[slot]->SetText(clip != nullptr ? StringView(clip->Path())
                                                        : StringView(u8"(missing)"));
        }

        void RefreshModeButton()
        {
            const StringView names[3] = { u8"Mode: Random (no repeat)", u8"Mode: Random",
                                          u8"Mode: Sequential" };
            m_modeButton->SetText(names[m_asset.mode % 3]);
        }

        // One REAL trigger: build a transient SoundCue from the slots (source files ->
        // in-memory clips, cached per page), resolve with the page's play state, play.
        void Audition()
        {
            if (m_audio == nullptr || m_audio->Engine() == nullptr) { return; }
            audio::SoundCue cue;
            cue.mode = static_cast<audio::SoundCueMode>(m_asset.mode);
            cue.pitchMin = m_asset.pitchMin;
            cue.pitchMax = m_asset.pitchMax;
            cue.volumeMin = m_asset.volumeMin;
            cue.volumeMax = m_asset.volumeMax;
            for (usize i = 0; i < audio::kSoundCueSlotCount; ++i)
            {
                audio::SoundCueVariant variant;
                variant.clip = LoadSlotClip(i);
                variant.weight = m_asset.weights[i];
                cue.variants.PushBack(Move(variant));
            }
            const audio::SoundCuePick pick =
                audio::ResolveSoundCue(cue, m_rng, m_lastVariant, m_sequentialCursor);
            if (pick.variantIndex < 0)
            {
                m_status->SetText(u8"No playable variant.");
                return;
            }
            m_lastVariant = pick.variantIndex;
            audio::AudioPlayParams params;
            params.pitch = pick.pitch;
            params.volume = pick.volume;
            params.allowDedupe = false;
            m_voice = m_audio->Engine()->Play(
                cue.variants[static_cast<usize>(pick.variantIndex)].clip, params);
            m_pickText = Format(u8"slot {}  pitch {}  vol {}", pick.variantIndex,
                                FormatFixed(pick.pitch, 2), FormatFixed(pick.volume, 2));
            m_status->SetText(m_pickText.AsView());
        }

        [[nodiscard]] RefPtr<audio::AudioClip> LoadSlotClip(usize slot)
        {
            const Guid& id = m_asset.clipIds[slot];
            if (id.IsNil() || m_context->Project() == nullptr) { return {}; }
            if (RefPtr<audio::AudioClip>* cached = m_clipCache.Find(id)) { return *cached; }
            draconic::content::Instance* instance =
                m_context->Project()->SourceDb().GetInstance(id);
            RefPtr<ISerializable> object =
                instance != nullptr ? instance->ReadObject() : RefPtr<ISerializable>{};
            auto* asset = Cast<audio::AudioClipAsset>(object.Get());
            if (asset == nullptr) { return {}; }
            const String path = PathJoin(m_context->Project()->SourcesRoot().AsView(),
                                         asset->fileName.AsView());
            Result<Array<byte>> bytes = ReadFile(path.AsView());
            if (!bytes.HasValue()) { return {}; }
            audio::AudioClipMetadata metadata;
            if (!audio::ProbeAudioClipMetadata(
                    Span<const byte>(bytes.Value().Data(), bytes.Value().Size()), metadata))
            {
                return {};
            }
            RefPtr<audio::AudioClip> clip = MakeRef<audio::AudioClip>(DefaultAllocator());
            clip->channels = metadata.channels;
            clip->sampleRate = metadata.sampleRate;
            clip->frameCount = metadata.frameCount;
            clip->durationSeconds = metadata.durationSeconds;
            clip->gain = asset->gain;
            clip->encodedData = Move(bytes.Value());
            m_clipCache.InsertOrAssign(id, clip);
            return clip;
        }

        EditorContext* m_context = nullptr;
        audio::AudioSubsystem* m_audio = nullptr;
        String m_title;
        audio::SoundCueAsset m_asset;
        Random m_rng;
        i32 m_lastVariant = -1;
        u32 m_sequentialCursor = 0;
        audio::VoiceHandle m_voice;
        String m_pickText;
        HashMap<Guid, RefPtr<audio::AudioClip>> m_clipCache;
        RefPtr<ui::View> m_content;
        RefPtr<ui::Label> m_slotLabels[audio::kSoundCueSlotCount];
        RefPtr<ui::NumericField> m_weightFields[audio::kSoundCueSlotCount];
        Array<RefPtr<ui::NumericField>> m_jitterFields;
        RefPtr<ui::Button> m_modeButton;
        RefPtr<ui::Label> m_status;
    };

    class SoundCuePageFactory final : public IEditorPageFactory
    {
    public:
        explicit SoundCuePageFactory(runtime::IApplicationHost& host) : m_host(&host) {}
        [[nodiscard]] const TypeInfo* PrimaryType() const override
        {
            return &audio::SoundCueAsset::StaticType();
        }
        [[nodiscard]] UniquePtr<EditorPage> CreatePage(EditorContext& context,
                                                       draconic::content::Instance& instance) override
        {
            auto* page = DefaultAllocator().New<SoundCueEditorPage>(context, *m_host, instance);
            return UniquePtr<EditorPage>(page, DefaultAllocator());
        }

    private:
        runtime::IApplicationHost* m_host;
    };
}
