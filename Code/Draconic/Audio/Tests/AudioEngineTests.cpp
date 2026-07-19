// draconic.audio core tests: the HEADLESS engine (no device - Update() pumps the mixer,
// so the whole voice state machine runs deterministically): handle validity across slot
// generations, the Traktor steal policy (free -> lower priority -> farthest same
// priority), fade-then-reap on stop, pause/resume, recent-play dedupe, buses, per-scene
// groups, streamed voices through the IAudioStreamSource seam, and the codec helpers.

#include <doctest/doctest.h>

#include "Core/Prelude.h"
#include <cmath>

import draconic.core;
import draconic.audio;

using namespace draconic::core;
using namespace draconic::audio;

namespace
{
    [[nodiscard]] Array<i16> MakeTone(f32 seconds, u32 sampleRate, u32 channels,
                                      f32 frequency = 440.0f, f32 amplitude = 0.5f)
    {
        Array<i16> samples;
        const usize frameCount = static_cast<usize>(seconds * static_cast<f32>(sampleRate));
        for (usize frame = 0; frame < frameCount; ++frame)
        {
            const f32 t = static_cast<f32>(frame) / static_cast<f32>(sampleRate);
            const f32 value = amplitude * std::sin(2.0f * 3.14159265f * frequency * t);
            const i16 sample = static_cast<i16>(value * 32000.0f);
            for (u32 channel = 0; channel < channels; ++channel) { samples.PushBack(sample); }
        }
        return samples;
    }

    [[nodiscard]] RefPtr<AudioClip> MakeToneClip(f32 seconds, u32 sampleRate = 8000,
                                                 u32 channels = 1)
    {
        const Array<i16> samples = MakeTone(seconds, sampleRate, channels);
        Array<byte> wav;
        REQUIRE(EncodeWavFromPcm16(Span<const i16>(samples.Data(), samples.Size()),
                                   channels, sampleRate, wav));
        RefPtr<AudioClip> clip = MakeRef<AudioClip>(DefaultAllocator());
        AudioClipMetadata metadata;
        REQUIRE(ProbeAudioClipMetadata(Span<const byte>(wav.Data(), wav.Size()), metadata));
        clip->channels = metadata.channels;
        clip->sampleRate = metadata.sampleRate;
        clip->frameCount = metadata.frameCount;
        clip->durationSeconds = metadata.durationSeconds;
        clip->encodedData = wav;
        return clip;
    }

    [[nodiscard]] AudioEngineSettings HeadlessSettings(u32 voiceCount = 8, u32 streamVoiceCount = 2,
                                                       f32 dedupeWindowSeconds = 0.0f)
    {
        AudioEngineSettings settings;
        settings.headless = true;
        settings.voiceCount = voiceCount;
        settings.streamVoiceCount = streamVoiceCount;
        settings.dedupeWindowSeconds = dedupeWindowSeconds;
        return settings;
    }

    // Re-openable in-memory stream source (what the cooked-content adapter does over paks).
    class MemoryStreamSource final : public IAudioStreamSource
    {
    public:
        explicit MemoryStreamSource(Array<byte> bytes) : m_bytes(Move(bytes)) {}
        [[nodiscard]] UniquePtr<IStream> OpenStream() override
        {
            UniquePtr<MemoryStream> stream = MakeUnique<MemoryStream>(DefaultAllocator());
            if (stream->Write(m_bytes.Data(), m_bytes.Size()) != m_bytes.Size()) { return {}; }
            (void)stream->Seek(0, SeekOrigin::Begin);
            return UniquePtr<IStream>(stream.Release(), DefaultAllocator());
        }

    private:
        Array<byte> m_bytes;
    };
}

TEST_CASE("audio.codec: wav encode -> probe -> decode round-trip")
{
    const Array<i16> samples = MakeTone(0.25f, 8000, 2);
    Array<byte> wav;
    REQUIRE(EncodeWavFromPcm16(Span<const i16>(samples.Data(), samples.Size()), 2, 8000, wav));

    AudioClipMetadata metadata;
    REQUIRE(ProbeAudioClipMetadata(Span<const byte>(wav.Data(), wav.Size()), metadata));
    CHECK(metadata.channels == 2u);
    CHECK(metadata.sampleRate == 8000u);
    CHECK(metadata.frameCount == samples.Size() / 2);
    CHECK(metadata.durationSeconds == doctest::Approx(0.25f).epsilon(0.01));

    Array<i16> decoded;
    AudioClipMetadata decodedMetadata;
    REQUIRE(DecodeAudioClipToPcm16(Span<const byte>(wav.Data(), wav.Size()), 0, decoded,
                                   decodedMetadata));
    REQUIRE(decoded.Size() == samples.Size());
    CHECK(decoded[100] == samples[100]);
    CHECK(decoded[101] == samples[101]);

    // Force-mono downmix halves the sample count and keeps the frame count.
    Array<i16> mono;
    AudioClipMetadata monoMetadata;
    REQUIRE(DecodeAudioClipToPcm16(Span<const byte>(wav.Data(), wav.Size()), 1, mono, monoMetadata));
    CHECK(monoMetadata.channels == 1u);
    CHECK(monoMetadata.frameCount == metadata.frameCount);
    CHECK(mono.Size() == metadata.frameCount);

    // Garbage bytes are rejected, not misread.
    Array<byte> garbage;
    for (int i = 0; i < 64; ++i) { garbage.PushBack(static_cast<byte>(i * 7)); }
    AudioClipMetadata rejected;
    CHECK_FALSE(ProbeAudioClipMetadata(Span<const byte>(garbage.Data(), garbage.Size()), rejected));
}

TEST_CASE("audio.engine: headless construction, empty/invalid plays are safely rejected")
{
    AudioEngine engine(HeadlessSettings());
    CHECK(engine.IsHeadless());
    CHECK(engine.ActiveVoiceCount() == 0u);

    CHECK_FALSE(engine.Play(RefPtr<AudioClip>{}).IsValid());
    RefPtr<AudioClip> empty = MakeRef<AudioClip>(DefaultAllocator());
    CHECK_FALSE(engine.Play(empty).IsValid());

    RefPtr<AudioClip> garbage = MakeRef<AudioClip>(DefaultAllocator());
    for (int i = 0; i < 64; ++i) { garbage->encodedData.PushBack(static_cast<byte>(i)); }
    garbage->channels = 1;
    CHECK_FALSE(engine.Play(garbage).IsValid());

    // Stale/foreign handles are inert everywhere.
    VoiceHandle bogus{ 3, 7 };
    CHECK_FALSE(engine.IsValidHandle(bogus));
    CHECK_FALSE(engine.IsPlaying(bogus));
    engine.Stop(bogus);
    engine.SetPaused(bogus, true);
    engine.SetVoiceVolume(bogus, 0.5f);
    engine.SetVoicePosition(bogus, Float3{ 1, 2, 3 }, Float3{ 0, 0, 0 });
    engine.Update(0.1f);
}

TEST_CASE("audio.engine: a one-shot plays, reaches its end, and reaps - the slot's "
          "generation invalidates the old handle on reuse")
{
    AudioEngine engine(HeadlessSettings());
    RefPtr<AudioClip> clip = MakeToneClip(0.1f);

    const VoiceHandle first = engine.Play(clip);
    REQUIRE(first.IsValid());
    CHECK(engine.IsPlaying(first));
    CHECK(engine.ActiveVoiceCount() == 1u);

    for (int i = 0; i < 6 && engine.IsValidHandle(first); ++i) { engine.Update(0.05f); }
    CHECK_FALSE(engine.IsValidHandle(first));
    CHECK(engine.ActiveVoiceCount() == 0u);

    // The freed slot comes back with a NEW generation: same slot, old handle stays dead.
    const VoiceHandle second = engine.Play(clip);
    REQUIRE(second.IsValid());
    CHECK(second.slot == first.slot);
    CHECK(second.generation != first.generation);
    CHECK_FALSE(engine.IsValidHandle(first));
    CHECK(engine.IsPlaying(second));
}

TEST_CASE("audio.engine: stop always fades (~10 ms) then reaps - the fade-then-reap "
          "state machine is observable")
{
    AudioEngine engine(HeadlessSettings());
    RefPtr<AudioClip> clip = MakeToneClip(0.5f);
    AudioPlayParams params;
    params.loop = true;

    const VoiceHandle voice = engine.Play(clip, params);
    REQUIRE(voice.IsValid());
    engine.Update(0.05f);
    CHECK(engine.IsPlaying(voice));

    engine.Stop(voice);
    VoiceStatus status;
    REQUIRE(engine.GetVoiceStatus(voice, status));
    CHECK(status.stopping);              // fading out, not yet reaped
    CHECK_FALSE(engine.IsPlaying(voice));
    CHECK(engine.IsValidHandle(voice));

    engine.Update(0.1f);                 // 100 ms >> the 10 ms fade
    CHECK_FALSE(engine.IsValidHandle(voice));
    CHECK(engine.ActiveVoiceCount() == 0u);
}

TEST_CASE("audio.engine: pause fades out but keeps the voice; resume fades back in")
{
    AudioEngine engine(HeadlessSettings());
    RefPtr<AudioClip> clip = MakeToneClip(0.3f);
    AudioPlayParams params;
    params.loop = true;

    const VoiceHandle voice = engine.Play(clip, params);
    REQUIRE(voice.IsValid());
    engine.SetPaused(voice, true);
    CHECK_FALSE(engine.IsPlaying(voice));
    CHECK(engine.IsValidHandle(voice));

    // A paused voice never reaps, no matter how long the engine runs.
    for (int i = 0; i < 10; ++i) { engine.Update(0.1f); }
    VoiceStatus status;
    REQUIRE(engine.GetVoiceStatus(voice, status));
    CHECK(status.paused);

    engine.SetPaused(voice, false);
    CHECK(engine.IsPlaying(voice));

    engine.Stop(voice);
    engine.Update(0.1f);
    CHECK_FALSE(engine.IsValidHandle(voice));
}

TEST_CASE("audio.engine: steal policy - free slot, then lowest lower priority, then "
          "farthest same priority; all-higher pools reject the play")
{
    AudioEngine engine(HeadlessSettings(/*voiceCount=*/2, /*streamVoiceCount=*/0));
    RefPtr<AudioClip> clipA = MakeToneClip(2.0f);
    RefPtr<AudioClip> clipB = MakeToneClip(2.0f, 8000, 1);
    RefPtr<AudioClip> clipC = MakeToneClip(2.0f, 4000, 1);
    RefPtr<AudioClip> clipD = MakeToneClip(2.0f, 16000, 1);

    AudioPlayParams low;
    low.priority = 10;
    low.loop = true;
    const VoiceHandle voiceA = engine.Play(clipA, low);
    low.priority = 20;
    const VoiceHandle voiceB = engine.Play(clipB, low);
    REQUIRE(voiceA.IsValid());
    REQUIRE(voiceB.IsValid());
    CHECK(engine.ActiveVoiceCount() == 2u);

    // Pool full: a HIGHER-priority play steals the LOWEST priority below it (A at 10).
    AudioPlayParams high;
    high.priority = 30;
    high.loop = true;
    const VoiceHandle voiceC = engine.Play(clipC, high);
    REQUIRE(voiceC.IsValid());
    CHECK_FALSE(engine.IsValidHandle(voiceA));
    CHECK(engine.IsValidHandle(voiceB));
    CHECK(engine.ActiveVoiceCount() == 2u);

    // Pool full of strictly-higher priorities: the new play is REJECTED.
    AudioPlayParams lowest;
    lowest.priority = 5;
    CHECK_FALSE(engine.Play(clipD, lowest).IsValid());
    CHECK(engine.IsValidHandle(voiceB));
    CHECK(engine.IsValidHandle(voiceC));
}

TEST_CASE("audio.engine: same-priority contention steals the voice FARTHEST from the listener")
{
    AudioEngine engine(HeadlessSettings(/*voiceCount=*/2, /*streamVoiceCount=*/0));
    engine.SetListenerTransform(Float3{ 0, 0, 0 }, Float3{ 0, 0, -1 }, Float3{ 0, 1, 0 },
                                Float3{ 0, 0, 0 });
    RefPtr<AudioClip> clipNear = MakeToneClip(2.0f);
    RefPtr<AudioClip> clipFar = MakeToneClip(2.0f, 8000, 1);
    RefPtr<AudioClip> clipNew = MakeToneClip(2.0f, 4000, 1);

    AudioPlayParams spatial;
    spatial.loop = true;
    spatial.spatial = true;
    spatial.priority = 50;
    spatial.position = Float3{ 1.0f, 0.0f, 0.0f };
    const VoiceHandle nearVoice = engine.Play(clipNear, spatial);
    spatial.position = Float3{ 60.0f, 0.0f, 0.0f };
    const VoiceHandle farVoice = engine.Play(clipFar, spatial);
    REQUIRE(nearVoice.IsValid());
    REQUIRE(farVoice.IsValid());

    spatial.position = Float3{ 2.0f, 0.0f, 0.0f };
    const VoiceHandle newVoice = engine.Play(clipNew, spatial);
    REQUIRE(newVoice.IsValid());
    CHECK(engine.IsValidHandle(nearVoice));       // near survived
    CHECK_FALSE(engine.IsValidHandle(farVoice));  // far was stolen
}

TEST_CASE("audio.engine: recent-play dedupe merges same-clip plays inside the window")
{
    AudioEngineSettings settings = HeadlessSettings();
    settings.dedupeWindowSeconds = 1.0f / 30.0f;
    AudioEngine engine(settings);
    RefPtr<AudioClip> clip = MakeToneClip(1.0f);
    AudioPlayParams params;
    params.loop = true;

    const VoiceHandle first = engine.Play(clip, params);
    const VoiceHandle merged = engine.Play(clip, params);   // same frame: merges
    REQUIRE(first.IsValid());
    CHECK(merged == first);
    CHECK(engine.ActiveVoiceCount() == 1u);

    engine.Update(0.1f);                                     // window expired
    const VoiceHandle second = engine.Play(clip, params);
    REQUIRE(second.IsValid());
    CHECK_FALSE(second == first);
    CHECK(engine.ActiveVoiceCount() == 2u);
}

TEST_CASE("audio.engine: dedupe opt-out - persistent sources sharing a clip stay distinct")
{
    // The component path plays with allowDedupe=false: four authored emitters sharing
    // one clip autoplay in the SAME instant and must each get their own voice (the
    // one-shot window silently collapsed all-but-one emitter - the AudioPlayground bug).
    AudioEngineSettings settings = HeadlessSettings();
    settings.dedupeWindowSeconds = 1.0f / 30.0f;
    AudioEngine engine(settings);
    RefPtr<AudioClip> clip = MakeToneClip(1.0f);
    AudioPlayParams params;
    params.loop = true;
    params.spatial = true;
    params.allowDedupe = false;

    VoiceHandle voices[4];
    for (int i = 0; i < 4; ++i)
    {
        params.position = Float3{ static_cast<f32>(i) * 10.0f, 0.0f, 0.0f };
        voices[i] = engine.Play(clip, params);
        REQUIRE(voices[i].IsValid());
    }
    CHECK(engine.ActiveVoiceCount() == 4u);
    for (int i = 1; i < 4; ++i) { CHECK_FALSE(voices[i] == voices[0]); }

    // Opt-out plays must not ARM the window either: a later defaulted (dedupe-eligible)
    // play still creates a fresh voice instead of merging into a persistent source.
    AudioPlayParams oneShot;
    const VoiceHandle shot = engine.Play(clip, oneShot);
    REQUIRE(shot.IsValid());
    for (int i = 0; i < 4; ++i) { CHECK_FALSE(shot == voices[i]); }
    CHECK(engine.ActiveVoiceCount() == 5u);
}

TEST_CASE("audio.engine: voice parameter setters land (volume/pitch/pan/position/looping)")
{
    AudioEngine engine(HeadlessSettings());
    RefPtr<AudioClip> clip = MakeToneClip(0.5f);
    AudioPlayParams params;
    params.spatial = true;
    params.loop = true;
    params.pitch = 1.5f;                     // real resampling, not stored-and-ignored
    params.volume = 0.75f;

    const VoiceHandle voice = engine.Play(clip, params);
    REQUIRE(voice.IsValid());
    VoiceStatus status;
    REQUIRE(engine.GetVoiceStatus(voice, status));
    CHECK(status.pitch == doctest::Approx(1.5f));
    CHECK(status.volume == doctest::Approx(0.75f));
    CHECK(status.spatial);

    engine.SetVoicePitch(voice, 0.5f);
    engine.SetVoiceVolume(voice, 0.25f);
    engine.SetVoicePosition(voice, Float3{ 3, 4, 5 }, Float3{ 1, 0, 0 });
    REQUIRE(engine.GetVoiceStatus(voice, status));
    CHECK(status.pitch == doctest::Approx(0.5f));
    CHECK(status.volume == doctest::Approx(0.25f));
    CHECK(status.position.x == doctest::Approx(3.0f));
    CHECK(status.position.z == doctest::Approx(5.0f));

    engine.SetVoiceLooping(voice, false);
    for (int i = 0; i < 20 && engine.IsValidHandle(voice); ++i) { engine.Update(0.1f); }
    CHECK_FALSE(engine.IsValidHandle(voice));   // un-looped voice runs out and reaps
}

TEST_CASE("audio.engine: spatializing a stereo clip downmixes with a one-time warning "
          "(runtime mono-guard) and still plays")
{
    AudioEngine engine(HeadlessSettings());
    RefPtr<AudioClip> stereo = MakeToneClip(0.2f, 8000, 2);
    AudioPlayParams params;
    params.spatial = true;
    const VoiceHandle voice = engine.Play(stereo, params);
    REQUIRE(voice.IsValid());
    CHECK(engine.IsPlaying(voice));
    engine.Update(0.05f);
    engine.Stop(voice);
    engine.Update(0.1f);
    CHECK_FALSE(engine.IsValidHandle(voice));
}

TEST_CASE("audio.engine: bus volumes and mutes are independent and re-appliable")
{
    AudioEngine engine(HeadlessSettings());
    CHECK(engine.BusVolume(AudioBus::Master) == doctest::Approx(1.0f));
    engine.SetBusVolume(AudioBus::Music, 0.3f);
    CHECK(engine.BusVolume(AudioBus::Music) == doctest::Approx(0.3f));
    CHECK(engine.BusVolume(AudioBus::Effects) == doctest::Approx(1.0f));

    engine.SetBusMuted(AudioBus::Music, true);
    CHECK(engine.BusMuted(AudioBus::Music));
    CHECK(engine.BusVolume(AudioBus::Music) == doctest::Approx(0.3f));   // remembered
    engine.SetBusMuted(AudioBus::Music, false);
    CHECK_FALSE(engine.BusMuted(AudioBus::Music));
}

TEST_CASE("audio.engine: per-scene groups - pause halts the scene's voices in place, "
          "stop fades them out, destroy frees them immediately")
{
    AudioEngine engine(HeadlessSettings());
    RefPtr<AudioClip> clip = MakeToneClip(1.0f);
    const u64 sceneGroup = engine.CreateSceneGroup();
    REQUIRE(sceneGroup != 0u);

    AudioPlayParams params;
    params.loop = true;
    params.sceneGroup = sceneGroup;
    const VoiceHandle voice = engine.Play(clip, params);
    AudioPlayParams globalParams;
    globalParams.loop = true;
    RefPtr<AudioClip> globalClip = MakeToneClip(1.0f, 4000, 1);
    const VoiceHandle globalVoice = engine.Play(globalClip, globalParams);
    REQUIRE(voice.IsValid());
    REQUIRE(globalVoice.IsValid());

    engine.SetSceneGroupPaused(sceneGroup, true);
    CHECK(engine.IsSceneGroupPaused(sceneGroup));
    for (int i = 0; i < 5; ++i) { engine.Update(0.1f); }
    CHECK(engine.IsValidHandle(voice));           // held, not reaped
    CHECK(engine.IsValidHandle(globalVoice));     // untouched by the scene pause

    engine.SetSceneGroupPaused(sceneGroup, false);
    CHECK_FALSE(engine.IsSceneGroupPaused(sceneGroup));

    engine.StopSceneGroup(sceneGroup);
    engine.Update(0.2f);
    CHECK_FALSE(engine.IsValidHandle(voice));
    CHECK(engine.IsValidHandle(globalVoice));

    const VoiceHandle again = engine.Play(clip, params);
    REQUIRE(again.IsValid());
    engine.DestroySceneGroup(sceneGroup);
    CHECK_FALSE(engine.IsValidHandle(again));     // immediate teardown
    CHECK(engine.IsValidHandle(globalVoice));
}

TEST_CASE("audio.engine: streamed clips play from an IAudioStreamSource through the "
          "stream voice pool (the pak-facing seam)")
{
    AudioEngine engine(HeadlessSettings(/*voiceCount=*/2, /*streamVoiceCount=*/1));
    const Array<i16> samples = MakeTone(0.5f, 8000, 1);
    Array<byte> wav;
    REQUIRE(EncodeWavFromPcm16(Span<const i16>(samples.Data(), samples.Size()), 1, 8000, wav));

    RefPtr<AudioClip> clip = MakeRef<AudioClip>(DefaultAllocator());
    AudioClipMetadata metadata;
    REQUIRE(ProbeAudioClipMetadata(Span<const byte>(wav.Data(), wav.Size()), metadata));
    clip->channels = metadata.channels;
    clip->sampleRate = metadata.sampleRate;
    clip->frameCount = metadata.frameCount;
    clip->durationSeconds = metadata.durationSeconds;
    clip->stream = true;
    clip->streamSource = UniquePtr<IAudioStreamSource>(
        DefaultAllocator().New<MemoryStreamSource>(wav), DefaultAllocator());

    AudioPlayParams params;
    params.bus = AudioBus::Music;
    params.loop = true;
    const VoiceHandle voice = engine.Play(clip, params);
    REQUIRE(voice.IsValid());
    CHECK(voice.slot == 2u);                      // stream slots sit after the main pool
    CHECK(engine.IsPlaying(voice));
    engine.Update(0.1f);
    CHECK(engine.IsPlaying(voice));

    // The stream pool is its own contention domain: a second stream play cannot steal
    // a same-priority spatial=false voice... but CAN reject when full of higher priority.
    AudioPlayParams lower;
    lower.bus = AudioBus::Music;
    lower.priority = 1;
    RefPtr<AudioClip> other = MakeRef<AudioClip>(DefaultAllocator());
    other->channels = metadata.channels;
    other->sampleRate = metadata.sampleRate;
    other->frameCount = metadata.frameCount;
    other->durationSeconds = metadata.durationSeconds;
    other->stream = true;
    other->streamSource = UniquePtr<IAudioStreamSource>(
        DefaultAllocator().New<MemoryStreamSource>(wav), DefaultAllocator());
    CHECK_FALSE(engine.Play(other, lower).IsValid());

    engine.Stop(voice);
    engine.Update(0.2f);
    CHECK_FALSE(engine.IsValidHandle(voice));
}

TEST_CASE("audio.engine: StopAll fades every voice out")
{
    AudioEngine engine(HeadlessSettings());
    RefPtr<AudioClip> clipA = MakeToneClip(1.0f);
    RefPtr<AudioClip> clipB = MakeToneClip(1.0f, 4000, 1);
    AudioPlayParams params;
    params.loop = true;
    const VoiceHandle voiceA = engine.Play(clipA, params);
    const VoiceHandle voiceB = engine.Play(clipB, params);
    REQUIRE(voiceA.IsValid());
    REQUIRE(voiceB.IsValid());

    engine.StopAll();
    engine.Update(0.2f);
    CHECK(engine.ActiveVoiceCount() == 0u);
    CHECK_FALSE(engine.IsValidHandle(voiceA));
    CHECK_FALSE(engine.IsValidHandle(voiceB));
}

TEST_CASE("audio.waveform: peaks bucket the decoded signal; silence reads near zero")
{
    // Tone for the first half, silence for the second: front buckets sit near the tone
    // amplitude (~0.49 after 16-bit quantization), back buckets near zero.
    const u32 rate = 8000;
    Array<i16> samples = MakeTone(0.5f, rate, 1);
    const usize toneCount = samples.Size();
    for (usize i = 0; i < toneCount; ++i) { samples.PushBack(0); }
    Array<byte> wav;
    REQUIRE(EncodeWavFromPcm16(Span<const i16>(samples.Data(), samples.Size()), 1, rate, wav));

    Array<f32> peaks;
    REQUIRE(BuildWaveformPeaks(Span<const byte>(wav.Data(), wav.Size()), 16, peaks));
    REQUIRE(peaks.Size() == 16u);
    for (usize i = 0; i < 7; ++i)    // tone half (skip the boundary bucket)
    {
        CHECK(peaks[i] > 0.4f);
        CHECK(peaks[i] <= 1.0f);
    }
    for (usize i = 9; i < 16; ++i)   // silent half
    {
        CHECK(peaks[i] < 0.01f);
    }

    // Degenerate inputs refuse cleanly.
    Array<f32> none;
    CHECK_FALSE(BuildWaveformPeaks(Span<const byte>{}, 16, none));
    CHECK_FALSE(BuildWaveformPeaks(Span<const byte>(wav.Data(), wav.Size()), 0, none));
    const byte garbage[8] = {};
    CHECK_FALSE(BuildWaveformPeaks(Span<const byte>(garbage, 8), 16, none));
}

TEST_CASE("audio.engine: distance low-pass glides open -> floor across [min, max] distance")
{
    AudioEngine engine(HeadlessSettings());
    engine.SetListenerTransform(Float3{ 0, 0, 0 }, Float3{ 0, 0, -1 }, Float3{ 0, 1, 0 },
                                Float3{ 0, 0, 0 });
    RefPtr<AudioClip> clip = MakeToneClip(2.0f);

    AudioPlayParams params;
    params.loop = true;
    params.spatial = true;
    params.minDistance = 2.0f;
    params.maxDistance = 20.0f;
    params.distanceLowpassHz = 4000.0f;
    params.position = Float3{ 0.0f, 0.0f, -2.0f };   // at minDistance: fully open
    const VoiceHandle voice = engine.Play(clip, params);
    REQUIRE(voice.IsValid());
    engine.Update(1.0f / 60.0f);

    VoiceStatus status;
    REQUIRE(engine.GetVoiceStatus(voice, status));
    const f32 openCutoff = status.lowpassCutoffHz;
    CHECK(openCutoff > 15000.0f);   // inside minDistance = no audible muffling

    // Far: the cutoff lands on the floor.
    engine.SetVoicePosition(voice, Float3{ 0.0f, 0.0f, -20.0f }, Float3{});
    engine.Update(1.0f / 60.0f);
    REQUIRE(engine.GetVoiceStatus(voice, status));
    CHECK(status.lowpassCutoffHz == doctest::Approx(4000.0f).epsilon(0.02));

    // Midway: strictly between the endpoints (the glide is monotonic).
    engine.SetVoicePosition(voice, Float3{ 0.0f, 0.0f, -11.0f }, Float3{});
    engine.Update(1.0f / 60.0f);
    REQUIRE(engine.GetVoiceStatus(voice, status));
    CHECK(status.lowpassCutoffHz > 4100.0f);
    CHECK(status.lowpassCutoffHz < openCutoff - 100.0f);

    // 0 Hz disables the filter entirely - no node, no cutoff reported.
    AudioPlayParams unfiltered = params;
    unfiltered.distanceLowpassHz = 0.0f;
    const VoiceHandle plain = engine.Play(clip, unfiltered);
    REQUIRE(plain.IsValid());
    engine.Update(1.0f / 60.0f);
    REQUIRE(engine.GetVoiceStatus(plain, status));
    CHECK(status.lowpassCutoffHz == 0.0f);
}
