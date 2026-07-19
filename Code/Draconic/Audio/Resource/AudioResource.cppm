// Draconic::AudioResource - the `draconic.audio.resource` module.
//
// Cooked audio content (docs/design/audio.md §4):
//   * AudioClipSource - the cooked record: probed metadata + import intent, with the
//     ORIGINAL compressed container bytes in the instance's "data" stream (Traktor's
//     compressed-cook model - never PCM sidecars; miniaudio records/sniffs the decoder).
//   * AudioClipFactory - builds the runtime draconic::audio::AudioClip a component's
//     Ref<> binds. Non-streamed clips load the container bytes into memory; streamed
//     clips get a ContentInstanceStreamSource so miniaudio pages the bytes on demand
//     straight out of the content mount (pak included - Instance::ReadData seeks).
//
// BusLayoutResource (mixer-as-data) is P2; the P1 layout is the fixed enum in
// draconic.audio.

module;
#include "Core/Prelude.h"
#include "Core/Reflection/Reflect.h"

export module draconic.audio.resource;

import draconic.core;
import draconic.resource;
import draconic.content;
import draconic.audio;

using namespace draconic::core;
using namespace draconic::resource;

export namespace draconic::audio
{
    // Cooked clip record. The container bytes live in the "data" stream beside it.
    class AudioClipSource : public ISerializable
    {
        DRACONIC_OBJECT(AudioClipSource, ISerializable)
    public:
        u32 channels = 0;
        u32 sampleRate = 0;
        u64 frameCount = 0;
        f32 durationSeconds = 0.0f;
        f32 gain = 1.0f;
        bool loop = false;
        u64 loopStartFrame = 0;
        u64 loopEndFrame = 0;        // 0 = clip end
        bool stream = false;
        bool keepCompressed = false;
        String containerExtension;   // recorded decoder hint ("wav"/"ogg"/"mp3"/"flac")

        void Serialize(ISerializer& ar) override
        {
            draconic::core::Serialize(ar, "channels", channels);
            draconic::core::Serialize(ar, "sampleRate", sampleRate);
            draconic::core::Serialize(ar, "frameCount", frameCount);
            draconic::core::Serialize(ar, "durationSeconds", durationSeconds);
            draconic::core::Serialize(ar, "gain", gain);
            draconic::core::Serialize(ar, "loop", loop);
            draconic::core::Serialize(ar, "loopStartFrame", loopStartFrame);
            draconic::core::Serialize(ar, "loopEndFrame", loopEndFrame);
            draconic::core::Serialize(ar, "stream", stream);
            draconic::core::Serialize(ar, "keepCompressed", keepCompressed);
            draconic::core::Serialize(ar, "containerExtension", containerExtension);
        }
    };

    // Re-openable stream source over a cooked instance's "data" stream: each play opens
    // an independent seekable stream through the content mount (native dir or pak).
    // The content database owns the instance and outlives every resource product.
    class ContentInstanceStreamSource final : public IAudioStreamSource
    {
    public:
        explicit ContentInstanceStreamSource(draconic::content::Instance& instance) noexcept
            : m_instance(&instance) {}

        [[nodiscard]] UniquePtr<IStream> OpenStream() override
        {
            return m_instance->ReadData(u8"data");
        }

    private:
        draconic::content::Instance* m_instance;
    };

    class AudioClipFactory final : public IResourceFactory
    {
    public:
        [[nodiscard]] const TypeInfo* ProductType() const override
        {
            return &AudioClip::StaticType();
        }

        [[nodiscard]] RefPtr<Object> Create(ResourceManager&,
                                            draconic::content::Instance& instance) override
        {
            RefPtr<ISerializable> object = instance.ReadObject();
            AudioClipSource* source = Cast<AudioClipSource>(object.Get());
            if (source == nullptr) { return RefPtr<Object>{}; }

            RefPtr<AudioClip> clip = MakeRef<AudioClip>(DefaultAllocator());
            clip->channels = source->channels;
            clip->sampleRate = source->sampleRate;
            clip->frameCount = source->frameCount;
            clip->durationSeconds = source->durationSeconds;
            clip->gain = source->gain;
            clip->loop = source->loop;
            clip->loopStartFrame = source->loopStartFrame;
            clip->loopEndFrame = source->loopEndFrame;
            clip->stream = source->stream;
            clip->keepCompressed = source->keepCompressed;

            if (source->stream)
            {
                clip->streamSource = UniquePtr<IAudioStreamSource>(
                    DefaultAllocator().New<ContentInstanceStreamSource>(instance),
                    DefaultAllocator());
            }
            else
            {
                UniquePtr<IStream> data = instance.ReadData(u8"data");
                if (data.Get() == nullptr) { return RefPtr<Object>{}; }
                const i64 size = data->Size();
                if (size <= 0) { return RefPtr<Object>{}; }
                clip->encodedData.Resize(static_cast<usize>(size));
                if (data->Read(clip->encodedData.Data(), static_cast<u64>(size))
                    != static_cast<u64>(size))
                {
                    return RefPtr<Object>{};
                }
            }
            return clip;
        }
    };

    // Registers the cooked record + product types (content-DB construction by type name).
    inline void RegisterAudioResource()
    {
        GlobalTypeRegistry().Register(AudioClipSource::StaticType());
        RegisterSerializable<AudioClipSource>();
        GlobalTypeRegistry().Register(AudioClip::StaticType());
    }

    DRACONIC_DEFINE_OBJECT(AudioClipSource, "draconic::audio")
}
