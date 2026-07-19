// Draconic::AudioEditor - the `draconic.audio.editor` module (tooling).
//
// Source-side audio authoring + cook (docs/design/audio.md §5):
//   * AudioClipAsset (editor::Asset): references the copied source file + import
//     settings (stream / force-mono / loop+points / trim / normalize / gain).
//   * AudioClipAssetBuilder: transcode-free write-through v1 - validate + probe via the
//     draconic.audio codec helpers, and write the ORIGINAL container bytes as the "data"
//     stream. When a destructive option is on (force mono / trim trailing silence /
//     normalize) the source is decoded, processed, and re-encoded as WAV (the one
//     container we write); untouched sources cook byte-identical.
//   * AudioFileImporter (wav/ogg/mp3/flac) with AudioImportOptions: `Stream` auto-computes
//     at import (> 10 s or > 2 MB pre-checks the asset; the dialog toggle FORCES it),
//     WAV `smpl` loop points are detected and enable looping.
//
// Never linked by the runtime. miniaudio itself never appears here - the codec seam in
// draconic.audio keeps this module decoder-free.

module;
#include "Core/Prelude.h"
#include "Core/Log/Log.h"
#include "Core/Reflection/Reflect.h"
#include <initializer_list>

export module draconic.audio.editor;

import draconic.core;
import draconic.editor;
import draconic.editor.core;
import draconic.content;
import draconic.audio;
import draconic.audio.resource;

using namespace draconic::core;

export namespace draconic::audio
{
    namespace content = draconic::content;

    // Source asset: the audio file + how it should cook.
    class AudioClipAsset final : public draconic::editor::Asset
    {
        DRACONIC_OBJECT(AudioClipAsset, draconic::editor::Asset)
    public:
        bool stream = false;              // decode on the fly at runtime (music/ambience)
        bool keepCompressed = false;      // in-memory clips: decode on play, not on load
        bool forceMono = false;           // downmix at cook (the 3D-intent default)
        bool loop = false;
        u64 loopStartFrame = 0;           // loopEndFrame 0 = clip end
        u64 loopEndFrame = 0;
        bool trimTrailingSilence = false; // Traktor trick: drop the silent tail
        bool normalize = false;           // peak-normalize to -1 dBFS
        f32 gain = 1.0f;                  // authored gain (runtime fold, not baked)

        void Serialize(ISerializer& ar) override
        {
            draconic::editor::Asset::Serialize(ar);   // fileName
            draconic::core::Serialize(ar, "stream", stream);
            draconic::core::Serialize(ar, "keepCompressed", keepCompressed);
            draconic::core::Serialize(ar, "forceMono", forceMono);
            draconic::core::Serialize(ar, "loop", loop);
            draconic::core::Serialize(ar, "loopStartFrame", loopStartFrame);
            draconic::core::Serialize(ar, "loopEndFrame", loopEndFrame);
            draconic::core::Serialize(ar, "trimTrailingSilence", trimTrailingSilence);
            draconic::core::Serialize(ar, "normalize", normalize);
            draconic::core::Serialize(ar, "gain", gain);
        }
    };

    // ---- pure import helpers (unit-testable without a project) ----

    /// The Stream auto-default (§5): long or large sources stream, small SFX stay
    /// in-memory. Computed at import; the asset field stays editable afterwards.
    [[nodiscard]] inline bool ShouldStreamAudioByDefault(f32 durationSeconds, usize sizeBytes)
    {
        return durationSeconds > 10.0f || sizeBytes > usize(2) * 1024 * 1024;
    }

    /// Scans a RIFF/WAVE container for the first `smpl` sampler loop. True when found,
    /// with the loop's start/end sample frames. Pure byte walk - no decoder involved.
    [[nodiscard]] inline bool ParseWavSampleLoop(Span<const byte> wavBytes,
                                                 u64& outLoopStartFrame, u64& outLoopEndFrame)
    {
        auto readU32 = [&](usize offset) -> u32 {
            return static_cast<u32>(static_cast<u8>(wavBytes[offset]))
                 | static_cast<u32>(static_cast<u8>(wavBytes[offset + 1])) << 8
                 | static_cast<u32>(static_cast<u8>(wavBytes[offset + 2])) << 16
                 | static_cast<u32>(static_cast<u8>(wavBytes[offset + 3])) << 24;
        };
        auto tagIs = [&](usize offset, const char* tag) -> bool {
            return static_cast<char>(wavBytes[offset]) == tag[0]
                && static_cast<char>(wavBytes[offset + 1]) == tag[1]
                && static_cast<char>(wavBytes[offset + 2]) == tag[2]
                && static_cast<char>(wavBytes[offset + 3]) == tag[3];
        };
        if (wavBytes.Size() < 12 || !tagIs(0, "RIFF") || !tagIs(8, "WAVE")) { return false; }
        usize cursor = 12;
        while (cursor + 8 <= wavBytes.Size())
        {
            const u32 chunkSize = readU32(cursor + 4);
            if (tagIs(cursor, "smpl"))
            {
                // smpl layout: 36 bytes of sampler fields (numLoops at +28), then
                // 24-byte loop records (start at +8, end at +12 within the record).
                const usize body = cursor + 8;
                if (chunkSize >= 36 + 24 && body + 36 + 24 <= wavBytes.Size()
                    && readU32(body + 28) >= 1)
                {
                    outLoopStartFrame = readU32(body + 36 + 8);
                    outLoopEndFrame = readU32(body + 36 + 12);
                    return outLoopEndFrame > outLoopStartFrame;
                }
                return false;
            }
            cursor += 8 + chunkSize + (chunkSize & 1);   // chunks are word-aligned
        }
        return false;
    }

    // Cooks an AudioClipAsset -> AudioClipSource (+ "data" container-byte stream).
    class AudioClipAssetBuilder final : public draconic::editor::DefaultAssetBuilder
    {
    public:
        [[nodiscard]] const TypeInfo* AssetType() const override
        {
            return &AudioClipAsset::StaticType();
        }
        [[nodiscard]] const TypeInfo* ProductType() const override
        {
            return &AudioClipSource::StaticType();
        }
        [[nodiscard]] u32 Version() const override { return 1; }

        [[nodiscard]] Status Build(const draconic::editor::Asset& asset,
                                   draconic::editor::AssetBuildContext& ctx) override
        {
            const AudioClipAsset& audioAsset = static_cast<const AudioClipAsset&>(asset);
            if (ctx.output == nullptr) { return Status{ ErrorCode::InvalidArgument }; }

            Result<Array<byte>> bytes = ReadSourceBytes(ctx, audioAsset.fileName.AsView());
            if (!bytes.HasValue()) { return Status{ bytes.Error() }; }
            Array<byte>& container = bytes.Value();

            // Validate: undecodable sources fail the cook, they never reach runtime.
            AudioClipMetadata metadata;
            if (!ProbeAudioClipMetadata(Span<const byte>(container.Data(), container.Size()),
                                        metadata))
            {
                DRACONIC_LOG_ERROR(u8"Audio", u8"'{}' is not decodable audio - cook failed",
                                   audioAsset.fileName);
                return Status{ ErrorCode::InvalidArgument };
            }

            String extension = draconic::editor::FileExtensionLower(audioAsset.fileName.AsView());

            // Destructive options re-encode (decode -> process -> WAV). Everything else
            // writes the original container bytes through untouched.
            const bool transform = audioAsset.forceMono || audioAsset.trimTrailingSilence
                                || audioAsset.normalize;
            if (transform)
            {
                const Status processed = ApplyTransforms(audioAsset, container, metadata);
                if (!processed.IsOk()) { return processed; }
                extension = String(u8"wav");
            }

            AudioClipSource cooked;
            cooked.channels = metadata.channels;
            cooked.sampleRate = metadata.sampleRate;
            cooked.frameCount = metadata.frameCount;
            cooked.durationSeconds = metadata.durationSeconds;
            cooked.gain = audioAsset.gain;
            cooked.loop = audioAsset.loop;
            cooked.loopStartFrame = audioAsset.loopStartFrame;
            cooked.loopEndFrame = audioAsset.loopEndFrame;
            cooked.stream = audioAsset.stream;
            cooked.keepCompressed = audioAsset.keepCompressed;
            cooked.containerExtension = extension;

            const Status wrote = ctx.output->WriteObject(cooked);
            if (!wrote.IsOk()) { return wrote; }
            return ctx.output->WriteData(u8"data",
                                         Span<const byte>(container.Data(), container.Size()));
        }

    private:
        [[nodiscard]] static Status ApplyTransforms(const AudioClipAsset& asset,
                                                    Array<byte>& container,
                                                    AudioClipMetadata& metadata)
        {
            Array<i16> samples;
            AudioClipMetadata decoded;
            if (!DecodeAudioClipToPcm16(Span<const byte>(container.Data(), container.Size()),
                                        asset.forceMono ? 1u : 0u, samples, decoded))
            {
                return Status{ ErrorCode::InvalidArgument };
            }

            if (asset.trimTrailingSilence)
            {
                // Drop the tail below ~-60 dBFS, keeping a 10 ms pad (Traktor trick).
                constexpr i16 kSilenceThreshold = 33;
                usize lastAudibleFrame = 0;
                const usize frameCount = samples.Size() / decoded.channels;
                for (usize frame = frameCount; frame > 0; --frame)
                {
                    bool audible = false;
                    for (u32 channel = 0; channel < decoded.channels; ++channel)
                    {
                        const i16 sample = samples[(frame - 1) * decoded.channels + channel];
                        const i16 magnitude = sample < 0 ? static_cast<i16>(-sample) : sample;
                        if (magnitude > kSilenceThreshold) { audible = true; break; }
                    }
                    if (audible) { lastAudibleFrame = frame; break; }
                }
                const usize pad = decoded.sampleRate / 100;
                usize keepFrames = lastAudibleFrame + pad;
                if (keepFrames > frameCount) { keepFrames = frameCount; }
                samples.Resize(keepFrames * decoded.channels);
                decoded.frameCount = keepFrames;
                decoded.durationSeconds = decoded.sampleRate > 0
                    ? static_cast<f32>(static_cast<f64>(keepFrames) / decoded.sampleRate) : 0.0f;
            }

            if (asset.normalize && !samples.IsEmpty())
            {
                i32 peak = 0;
                for (i16 sample : samples)
                {
                    const i32 magnitude = sample < 0 ? -static_cast<i32>(sample) : sample;
                    if (magnitude > peak) { peak = magnitude; }
                }
                if (peak > 0)
                {
                    const f32 target = 0.891f * 32767.0f;   // -1 dBFS headroom
                    const f32 scale = target / static_cast<f32>(peak);
                    for (i16& sample : samples)
                    {
                        f32 scaled = static_cast<f32>(sample) * scale;
                        if (scaled > 32767.0f) { scaled = 32767.0f; }
                        if (scaled < -32768.0f) { scaled = -32768.0f; }
                        sample = static_cast<i16>(scaled);
                    }
                }
            }

            Array<byte> wav;
            if (!EncodeWavFromPcm16(Span<const i16>(samples.Data(), samples.Size()),
                                    decoded.channels, decoded.sampleRate, wav))
            {
                return Status{ ErrorCode::InvalidArgument };
            }
            container = Move(wav);
            metadata = decoded;
            return Status{};
        }
    };

    // Import-dialog options (all toggles; the Stream auto-default is computed from the
    // probed file at import, so the checkbox here is a FORCE, not the whole story).
    class AudioImportOptions final : public draconic::editor::ImportOptions
    {
        DRACONIC_OBJECT(AudioImportOptions, draconic::editor::ImportOptions)
    public:
        bool stream = false;
        bool forceMono = false;
        bool loop = false;
        bool trimTrailingSilence = false;
        bool normalize = false;

        [[nodiscard]] Array<Toggle> Toggles() override
        {
            Array<Toggle> toggles;
            toggles.PushBack(Toggle{ u8"Stream",
                u8"Decode on the fly at runtime (auto-enabled for sources over 10 s / 2 MB)",
                &stream });
            toggles.PushBack(Toggle{ u8"Force mono",
                u8"Downmix to one channel at cook (recommended for 3D-positioned sounds)",
                &forceMono });
            toggles.PushBack(Toggle{ u8"Loop",
                u8"Loop by default when played (WAV smpl loop points are detected automatically)",
                &loop });
            toggles.PushBack(Toggle{ u8"Trim trailing silence",
                u8"Drop the silent tail at cook", &trimTrailingSilence });
            toggles.PushBack(Toggle{ u8"Normalize",
                u8"Peak-normalize to -1 dBFS at cook", &normalize });
            return toggles;
        }
    };

    // OS-file importer (editor drag-drop): copies the audio file into Sources/ and
    // creates an AudioClipAsset named after the file stem.
    class AudioFileImporter final : public draconic::editor::IFileImporter
    {
    public:
        [[nodiscard]] StringView Label() const override { return u8"Audio"; }

        [[nodiscard]] bool Accepts(StringView extension) const override
        {
            for (StringView candidate : { u8"wav", u8"ogg", u8"mp3", u8"flac" })
            {
                if (extension == candidate) { return true; }
            }
            return false;
        }

        [[nodiscard]] RefPtr<draconic::editor::ImportOptions> CreateOptions() const override
        {
            return MakeRef<AudioImportOptions>(DefaultAllocator());
        }

        [[nodiscard]] Result<content::Instance*> Import(
            StringView sourcePath, draconic::editor::EditorProject& project,
            content::Group& group, const draconic::editor::ImportOptions* options,
            Object*, Array<draconic::editor::DeferredImportWrite>*) override
        {
            Result<Array<byte>> bytes = ReadFile(sourcePath);
            if (!bytes.HasValue()) { return Err(bytes.Error()); }

            AudioClipMetadata metadata;
            if (!ProbeAudioClipMetadata(
                    Span<const byte>(bytes.Value().Data(), bytes.Value().Size()), metadata))
            {
                DRACONIC_LOG_ERROR(u8"Audio", u8"'{}' is not decodable audio - import refused",
                                   sourcePath);
                return Err(ErrorCode::InvalidArgument);
            }

            Result<String> fileName = draconic::editor::CopyIntoSources(project, sourcePath);
            if (!fileName.HasValue()) { return Err(fileName.Error()); }

            const StringView stem = draconic::editor::FileStemOf(fileName.Value().AsView());
            content::Instance* instance =
                group.CreateInstance(stem, AudioClipAsset::StaticType());
            if (instance == nullptr) { return Err(ErrorCode::Unknown); }

            const auto* audioOptions = static_cast<const AudioImportOptions*>(options);
            AudioClipAsset asset;
            asset.fileName = fileName.Value();
            asset.stream = (audioOptions != nullptr && audioOptions->stream)
                || ShouldStreamAudioByDefault(metadata.durationSeconds, bytes.Value().Size());
            if (audioOptions != nullptr)
            {
                asset.forceMono = audioOptions->forceMono;
                asset.loop = audioOptions->loop;
                asset.trimTrailingSilence = audioOptions->trimTrailingSilence;
                asset.normalize = audioOptions->normalize;
            }

            // WAV smpl loop points: authored loops win over the checkbox default.
            u64 loopStart = 0;
            u64 loopEnd = 0;
            if (draconic::editor::FileExtensionLower(sourcePath) == u8"wav"
                && ParseWavSampleLoop(
                       Span<const byte>(bytes.Value().Data(), bytes.Value().Size()),
                       loopStart, loopEnd))
            {
                asset.loop = true;
                asset.loopStartFrame = loopStart;
                asset.loopEndFrame = loopEnd;
            }

            const Status written = instance->WriteObject(asset);
            if (!written.IsOk()) { return Err(written.Code()); }
            return instance;
        }
    };

    // ---- bus layout asset (P2): the mixer edited in the inspector ----
    // FLAT per-bus fields (v1) so the reflection inspector edits it without an array
    // editor: per bus - volume, mute, and three effect slots (0 disables each). The
    // builder folds the flat fields into the generic wire chain (lowpass -> highpass ->
    // delay, in that order, when enabled).

    class AudioBusLayoutAsset final : public draconic::editor::Asset
    {
        DRACONIC_OBJECT(AudioBusLayoutAsset, draconic::editor::Asset)
    public:
        struct Bus
        {
            f32 volume = 1.0f;
            bool muted = false;
            f32 lowpassHz = 0.0f;      // 0 = off
            f32 highpassHz = 0.0f;     // 0 = off
            f32 delaySeconds = 0.0f;   // 0 = off
            f32 delayDecay = 0.3f;
        };
        Bus master;
        Bus effects;
        Bus music;
        Bus ui;

        void Serialize(ISerializer& ar) override
        {
            draconic::editor::Asset::Serialize(ar);
            auto serializeBus = [&ar](const char* prefix, Bus& bus) {
                (void)prefix;
                draconic::core::Serialize(ar, "volume", bus.volume);
                draconic::core::Serialize(ar, "muted", bus.muted);
                draconic::core::Serialize(ar, "lowpassHz", bus.lowpassHz);
                draconic::core::Serialize(ar, "highpassHz", bus.highpassHz);
                draconic::core::Serialize(ar, "delaySeconds", bus.delaySeconds);
                draconic::core::Serialize(ar, "delayDecay", bus.delayDecay);
            };
            serializeBus("master", master);
            serializeBus("effects", effects);
            serializeBus("music", music);
            serializeBus("ui", ui);
        }
    };

    class AudioBusLayoutAssetBuilder final : public draconic::editor::DefaultAssetBuilder
    {
    public:
        [[nodiscard]] const TypeInfo* AssetType() const override
        {
            return &AudioBusLayoutAsset::StaticType();
        }
        [[nodiscard]] const TypeInfo* ProductType() const override
        {
            return &AudioBusLayoutSource::StaticType();
        }
        [[nodiscard]] u32 Version() const override { return 1; }

        [[nodiscard]] Status Build(const draconic::editor::Asset& asset,
                                   draconic::editor::AssetBuildContext& ctx) override
        {
            const auto& layoutAsset = static_cast<const AudioBusLayoutAsset&>(asset);
            if (ctx.output == nullptr) { return Status{ ErrorCode::InvalidArgument }; }

            AudioBusLayoutSource source;
            const AudioBusLayoutAsset::Bus* buses[static_cast<usize>(AudioBus::Count)] = {};
            buses[static_cast<usize>(AudioBus::Master)] = &layoutAsset.master;
            buses[static_cast<usize>(AudioBus::Effects)] = &layoutAsset.effects;
            buses[static_cast<usize>(AudioBus::Music)] = &layoutAsset.music;
            buses[static_cast<usize>(AudioBus::UI)] = &layoutAsset.ui;
            for (usize i = 0; i < static_cast<usize>(AudioBus::Count); ++i)
            {
                const AudioBusLayoutAsset::Bus& bus = *buses[i];
                AudioBusSettings& out = source.layout.buses[i];
                out.volume = Clamp(bus.volume, 0.0f, 4.0f);
                out.muted = bus.muted;
                if (bus.lowpassHz > 0.0f)
                {
                    AudioBusEffectDesc effect;
                    effect.kind = AudioBusEffectKind::Lowpass;
                    effect.frequencyHz = bus.lowpassHz;
                    out.effects.PushBack(effect);
                }
                if (bus.highpassHz > 0.0f)
                {
                    AudioBusEffectDesc effect;
                    effect.kind = AudioBusEffectKind::Highpass;
                    effect.frequencyHz = bus.highpassHz;
                    out.effects.PushBack(effect);
                }
                if (bus.delaySeconds > 0.0f)
                {
                    AudioBusEffectDesc effect;
                    effect.kind = AudioBusEffectKind::Delay;
                    effect.delaySeconds = bus.delaySeconds;
                    effect.delayDecay = Clamp(bus.delayDecay, 0.0f, 0.99f);
                    if (bus.delayDecay >= 1.0f)
                    {
                        DRACONIC_LOG_WARNING(u8"Audio",
                            u8"bus layout '{}': delayDecay >= 1 self-oscillates - clamped to 0.99",
                            asset.fileName);
                    }
                    out.effects.PushBack(effect);
                }
            }

            const Status written = ctx.output->WriteObject(source);
            return written;
        }
    };

    // Registers the asset type for content-DB construction + deserialization.
    inline void RegisterAudioAssets()
    {
        GlobalTypeRegistry().Register(AudioClipAsset::StaticType());
        RegisterSerializable<AudioClipAsset>();
        GlobalTypeRegistry().Register(AudioBusLayoutAsset::StaticType());
        RegisterSerializable<AudioBusLayoutAsset>();
    }

    DRACONIC_DEFINE_OBJECT(AudioClipAsset, "draconic::audio")
    DRACONIC_DEFINE_OBJECT(AudioImportOptions, "draconic::audio")
    DRACONIC_DEFINE_OBJECT(AudioBusLayoutAsset, "draconic::audio")
}
