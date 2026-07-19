// Full audio pipeline: author a wav (sources mount) -> AudioClipAsset -> cook via the
// builder into an output db -> load the AudioClip product through the factory. Covers
// write-through byte identity (NO PCM sidecars - the container bytes ARE the cook), the
// stream flag's ContentInstanceStreamSource wiring, cook validation, the destructive
// transforms (force-mono / trim / normalize), the Stream auto-default, the WAV `smpl`
// loop-point parser, and the importer's asset fan-out through a real EditorProject.

#include <doctest/doctest.h>

#include "Core/Prelude.h"
#include <cmath>
#include <initializer_list>

import draconic.core;
import draconic.vfs;
import draconic.content;
import draconic.resource;
import draconic.editor;
import draconic.editor.core;
import draconic.audio;
import draconic.audio.resource;
import draconic.audio.editor;

using namespace draconic::core;
using namespace draconic::resource;
using namespace draconic::audio;
namespace content = draconic::content;

namespace
{
    [[nodiscard]] Array<i16> MakeTone(f32 seconds, u32 sampleRate, u32 channels,
                                      f32 amplitude = 0.5f)
    {
        Array<i16> samples;
        const usize frameCount = static_cast<usize>(seconds * static_cast<f32>(sampleRate));
        for (usize frame = 0; frame < frameCount; ++frame)
        {
            const f32 t = static_cast<f32>(frame) / static_cast<f32>(sampleRate);
            const i16 sample = static_cast<i16>(
                amplitude * std::sin(2.0f * 3.14159265f * 440.0f * t) * 32000.0f);
            for (u32 channel = 0; channel < channels; ++channel) { samples.PushBack(sample); }
        }
        return samples;
    }

    [[nodiscard]] Array<byte> MakeToneWav(f32 seconds, u32 sampleRate = 8000, u32 channels = 1,
                                          f32 amplitude = 0.5f)
    {
        const Array<i16> samples = MakeTone(seconds, sampleRate, channels, amplitude);
        Array<byte> wav;
        REQUIRE(EncodeWavFromPcm16(Span<const i16>(samples.Data(), samples.Size()),
                                   channels, sampleRate, wav));
        return wav;
    }

    // Appends an `smpl` chunk with one loop and patches the RIFF size.
    void AppendSampleLoopChunk(Array<byte>& wav, u32 loopStart, u32 loopEnd)
    {
        auto pushU32 = [&](u32 value) {
            wav.PushBack(static_cast<byte>(value & 0xFF));
            wav.PushBack(static_cast<byte>((value >> 8) & 0xFF));
            wav.PushBack(static_cast<byte>((value >> 16) & 0xFF));
            wav.PushBack(static_cast<byte>((value >> 24) & 0xFF));
        };
        auto pushTag = [&](const char* tag) {
            for (int i = 0; i < 4; ++i) { wav.PushBack(static_cast<byte>(tag[i])); }
        };
        pushTag("smpl");
        pushU32(36 + 24);                 // chunk size: sampler fields + one loop record
        for (int i = 0; i < 7; ++i) { pushU32(0); }   // manufacturer .. SMPTE offset
        pushU32(1);                       // numLoops
        pushU32(0);                       // sampler data
        pushU32(0);                       // loop id
        pushU32(0);                       // loop type (forward)
        pushU32(loopStart);
        pushU32(loopEnd);
        pushU32(0);                       // fraction
        pushU32(0);                       // play count (infinite)
        // Patch the RIFF size field (bytes 4..7) = file size - 8.
        const u32 riffSize = static_cast<u32>(wav.Size()) - 8;
        wav[4] = static_cast<byte>(riffSize & 0xFF);
        wav[5] = static_cast<byte>((riffSize >> 8) & 0xFF);
        wav[6] = static_cast<byte>((riffSize >> 16) & 0xFF);
        wav[7] = static_cast<byte>((riffSize >> 24) & 0xFF);
    }

    void RemoveDbTree(StringView dir)
    {
        for (const utf8char* name : { u8"clip.rasset", u8"cooked.rasset", u8"cooked.data.bin",
                                      u8"tone.wav" })
        {
            String path(dir);
            path.Append(u8"/");
            path.Append(name);
            FileDelete(path.AsView());
        }
        RemoveDirectory(dir);
    }
}

TEST_CASE("audio.pipeline: Stream auto-default - long OR large sources stream")
{
    CHECK_FALSE(ShouldStreamAudioByDefault(1.0f, 100 * 1024));
    CHECK(ShouldStreamAudioByDefault(11.0f, 100 * 1024));            // long
    CHECK(ShouldStreamAudioByDefault(1.0f, 3 * 1024 * 1024));        // large
    CHECK_FALSE(ShouldStreamAudioByDefault(10.0f, 2 * 1024 * 1024)); // exactly at the line
}

TEST_CASE("audio.pipeline: WAV smpl loop points parse (and absent/garbage inputs do not)")
{
    Array<byte> wav = MakeToneWav(0.2f);
    u64 loopStart = 0;
    u64 loopEnd = 0;
    CHECK_FALSE(ParseWavSampleLoop(Span<const byte>(wav.Data(), wav.Size()), loopStart, loopEnd));

    AppendSampleLoopChunk(wav, 100, 1500);
    REQUIRE(ParseWavSampleLoop(Span<const byte>(wav.Data(), wav.Size()), loopStart, loopEnd));
    CHECK(loopStart == 100u);
    CHECK(loopEnd == 1500u);

    Array<byte> garbage;
    for (int i = 0; i < 128; ++i) { garbage.PushBack(static_cast<byte>(i)); }
    CHECK_FALSE(ParseWavSampleLoop(Span<const byte>(garbage.Data(), garbage.Size()),
                                   loopStart, loopEnd));
}

TEST_CASE("audio.pipeline: wav -> AudioClipAsset cook -> AudioClip keeps the ORIGINAL "
          "container bytes (write-through, no PCM sidecar)")
{
    RegisterAudioResource();
    RegisterAudioAssets();
    RemoveDbTree(u8"draconic_audiopipe_src");
    RemoveDbTree(u8"draconic_audiopipe_out");

    draconic::vfs::NativeFileSystem sourceMount(u8"draconic_audiopipe_src");
    draconic::vfs::NativeFileSystem outputMount(u8"draconic_audiopipe_out");
    content::ContentDatabase outputDb(outputMount, BinarySerializerFactory(), u8".rasset");

    const Array<byte> wav = MakeToneWav(0.25f, 8000, 2);
    REQUIRE(CreateDirectory(u8"draconic_audiopipe_src"));
    REQUIRE(WriteFile(u8"draconic_audiopipe_src/tone.wav",
                      Span<const byte>(wav.Data(), wav.Size())).IsOk());

    AudioClipAsset asset;
    asset.fileName = String(u8"tone.wav");
    asset.gain = 0.8f;
    asset.loop = true;
    asset.loopStartFrame = 10;
    asset.loopEndFrame = 900;

    AudioClipAssetBuilder builder;
    draconic::editor::AssetBuildContext ctx;
    ctx.sources = &sourceMount;
    auto* outputInstance =
        outputDb.RootGroup()->CreateInstance(u8"cooked", AudioClipSource::StaticType());
    ctx.output = outputInstance;
    REQUIRE(builder.Build(asset, ctx).IsOk());

    AudioClipFactory factory;
    ResourceManager manager(outputDb);
    manager.AddFactory(&factory);
    Proxy<AudioClip> clip = manager.Bind<AudioClip>(outputInstance->Id());
    REQUIRE(clip);
    CHECK(clip->channels == 2u);
    CHECK(clip->sampleRate == 8000u);
    CHECK(clip->durationSeconds == doctest::Approx(0.25f).epsilon(0.01));
    CHECK(clip->gain == doctest::Approx(0.8f));
    CHECK(clip->loop);
    CHECK(clip->loopStartFrame == 10u);
    CHECK(clip->loopEndFrame == 900u);
    CHECK_FALSE(clip->stream);
    REQUIRE(clip->encodedData.Size() == wav.Size());   // byte-identical write-through
    bool identical = true;
    for (usize i = 0; i < wav.Size(); ++i)
    {
        if (clip->encodedData[i] != wav[i]) { identical = false; break; }
    }
    CHECK(identical);

    RemoveDbTree(u8"draconic_audiopipe_src");
    RemoveDbTree(u8"draconic_audiopipe_out");
}

TEST_CASE("audio.pipeline: stream-flagged cooks bind a re-openable content stream source")
{
    RegisterAudioResource();
    RegisterAudioAssets();
    RemoveDbTree(u8"draconic_audiopipe_stream_src");
    RemoveDbTree(u8"draconic_audiopipe_stream_out");

    draconic::vfs::NativeFileSystem sourceMount(u8"draconic_audiopipe_stream_src");
    draconic::vfs::NativeFileSystem outputMount(u8"draconic_audiopipe_stream_out");
    content::ContentDatabase outputDb(outputMount, BinarySerializerFactory(), u8".rasset");

    const Array<byte> wav = MakeToneWav(0.5f);
    REQUIRE(CreateDirectory(u8"draconic_audiopipe_stream_src"));
    REQUIRE(WriteFile(u8"draconic_audiopipe_stream_src/tone.wav",
                      Span<const byte>(wav.Data(), wav.Size())).IsOk());

    AudioClipAsset asset;
    asset.fileName = String(u8"tone.wav");
    asset.stream = true;

    AudioClipAssetBuilder builder;
    draconic::editor::AssetBuildContext ctx;
    ctx.sources = &sourceMount;
    auto* outputInstance =
        outputDb.RootGroup()->CreateInstance(u8"cooked", AudioClipSource::StaticType());
    ctx.output = outputInstance;
    REQUIRE(builder.Build(asset, ctx).IsOk());

    AudioClipFactory factory;
    ResourceManager manager(outputDb);
    manager.AddFactory(&factory);
    Proxy<AudioClip> clip = manager.Bind<AudioClip>(outputInstance->Id());
    REQUIRE(clip);
    CHECK(clip->stream);
    CHECK(clip->encodedData.IsEmpty());              // no bytes held in memory
    REQUIRE(clip->streamSource.Get() != nullptr);

    // The source re-opens independently and serves the exact container bytes.
    for (int pass = 0; pass < 2; ++pass)
    {
        UniquePtr<IStream> stream = clip->streamSource->OpenStream();
        REQUIRE(stream.Get() != nullptr);
        REQUIRE(stream->Size() == static_cast<i64>(wav.Size()));
        Array<byte> readBack;
        readBack.Resize(wav.Size());
        REQUIRE(stream->Read(readBack.Data(), wav.Size()) == wav.Size());
        CHECK(readBack[40] == wav[40]);
    }

    RemoveDbTree(u8"draconic_audiopipe_stream_src");
    RemoveDbTree(u8"draconic_audiopipe_stream_out");
}

TEST_CASE("audio.pipeline: the builder VALIDATES - undecodable sources fail the cook")
{
    RegisterAudioResource();
    RegisterAudioAssets();
    RemoveDbTree(u8"draconic_audiopipe_bad_src");
    RemoveDbTree(u8"draconic_audiopipe_bad_out");

    draconic::vfs::NativeFileSystem sourceMount(u8"draconic_audiopipe_bad_src");
    draconic::vfs::NativeFileSystem outputMount(u8"draconic_audiopipe_bad_out");
    content::ContentDatabase outputDb(outputMount, BinarySerializerFactory(), u8".rasset");

    Array<byte> garbage;
    for (int i = 0; i < 256; ++i) { garbage.PushBack(static_cast<byte>(i * 3)); }
    REQUIRE(CreateDirectory(u8"draconic_audiopipe_bad_src"));
    REQUIRE(WriteFile(u8"draconic_audiopipe_bad_src/tone.wav",
                      Span<const byte>(garbage.Data(), garbage.Size())).IsOk());

    AudioClipAsset asset;
    asset.fileName = String(u8"tone.wav");
    AudioClipAssetBuilder builder;
    draconic::editor::AssetBuildContext ctx;
    ctx.sources = &sourceMount;
    ctx.output = outputDb.RootGroup()->CreateInstance(u8"cooked", AudioClipSource::StaticType());
    CHECK_FALSE(builder.Build(asset, ctx).IsOk());

    RemoveDbTree(u8"draconic_audiopipe_bad_src");
    RemoveDbTree(u8"draconic_audiopipe_bad_out");
}

TEST_CASE("audio.pipeline: destructive options - force-mono downmixes, trim drops the "
          "silent tail, normalize lifts the peak to -1 dBFS")
{
    RegisterAudioResource();
    RegisterAudioAssets();
    RemoveDbTree(u8"draconic_audiopipe_fx_src");
    RemoveDbTree(u8"draconic_audiopipe_fx_out");

    draconic::vfs::NativeFileSystem sourceMount(u8"draconic_audiopipe_fx_src");
    draconic::vfs::NativeFileSystem outputMount(u8"draconic_audiopipe_fx_out");
    content::ContentDatabase outputDb(outputMount, BinarySerializerFactory(), u8".rasset");

    // A quiet stereo tone with half a second of pure silence appended.
    Array<i16> samples = MakeTone(0.25f, 8000, 2, 0.1f);
    const usize toneFrames = samples.Size() / 2;
    for (usize i = 0; i < 8000 / 2 * 2; ++i) { samples.PushBack(0); }
    Array<byte> wav;
    REQUIRE(EncodeWavFromPcm16(Span<const i16>(samples.Data(), samples.Size()), 2, 8000, wav));
    REQUIRE(CreateDirectory(u8"draconic_audiopipe_fx_src"));
    REQUIRE(WriteFile(u8"draconic_audiopipe_fx_src/tone.wav",
                      Span<const byte>(wav.Data(), wav.Size())).IsOk());

    AudioClipAsset asset;
    asset.fileName = String(u8"tone.wav");
    asset.forceMono = true;
    asset.trimTrailingSilence = true;
    asset.normalize = true;

    AudioClipAssetBuilder builder;
    draconic::editor::AssetBuildContext ctx;
    ctx.sources = &sourceMount;
    auto* outputInstance =
        outputDb.RootGroup()->CreateInstance(u8"cooked", AudioClipSource::StaticType());
    ctx.output = outputInstance;
    REQUIRE(builder.Build(asset, ctx).IsOk());

    AudioClipFactory factory;
    ResourceManager manager(outputDb);
    manager.AddFactory(&factory);
    Proxy<AudioClip> clip = manager.Bind<AudioClip>(outputInstance->Id());
    REQUIRE(clip);
    CHECK(clip->channels == 1u);                              // downmixed
    CHECK(clip->frameCount < toneFrames + 4000u / 2u);        // tail trimmed
    CHECK(clip->frameCount >= toneFrames);                    // tone kept

    Array<i16> cookedSamples;
    AudioClipMetadata cookedMetadata;
    REQUIRE(DecodeAudioClipToPcm16(clip->EncodedBytes(), 0, cookedSamples, cookedMetadata));
    i32 peak = 0;
    for (i16 sample : cookedSamples)
    {
        const i32 magnitude = sample < 0 ? -static_cast<i32>(sample) : sample;
        if (magnitude > peak) { peak = magnitude; }
    }
    CHECK(peak > 27000);                                      // ~-1 dBFS (was ~3200)

    RemoveDbTree(u8"draconic_audiopipe_fx_src");
    RemoveDbTree(u8"draconic_audiopipe_fx_out");
}

TEST_CASE("audio.pipeline: the file importer creates an AudioClipAsset with probed "
          "defaults (stream auto for long sources, smpl loops detected)")
{
    RegisterAudioResource();
    RegisterAudioAssets();
    const StringView projectDir = u8"draconic_audiopipe_project";
    auto cleanProject = [&]() {
        FileDelete(PathJoin(projectDir, u8"Project.xml"));
        FileDelete(PathJoin(projectDir, u8"Sources/short.wav"));
        FileDelete(PathJoin(projectDir, u8"Sources/long.wav"));
        FileDelete(PathJoin(projectDir, u8"Content/draconic_audiopipe_short.xasset"));
        FileDelete(PathJoin(projectDir, u8"Content/draconic_audiopipe_long.xasset"));
        for (StringView sub : { u8"Content", u8"Sources", u8"Cooked", u8"Editor", u8".cache" })
        {
            RemoveDirectory(PathJoin(projectDir, sub));
        }
        RemoveDirectory(projectDir);
    };
    cleanProject();
    REQUIRE(draconic::editor::EditorProject::Create(projectDir, u8"AudioTest").IsOk());
    UniquePtr<draconic::editor::EditorProject> project =
        draconic::editor::EditorProject::Open(projectDir);
    REQUIRE(static_cast<bool>(project));

    AudioFileImporter importer;
    CHECK(importer.Accepts(u8"wav"));
    CHECK(importer.Accepts(u8"ogg"));
    CHECK(importer.Accepts(u8"mp3"));
    CHECK(importer.Accepts(u8"flac"));
    CHECK_FALSE(importer.Accepts(u8"png"));

    // Short SFX with an authored smpl loop: stays in-memory, loop points imported.
    Array<byte> shortWav = MakeToneWav(0.25f, 8000, 1);
    AppendSampleLoopChunk(shortWav, 50, 1900);
    REQUIRE(WriteFile(u8"draconic_audiopipe_short.wav",
                      Span<const byte>(shortWav.Data(), shortWav.Size())).IsOk());
    Result<content::Instance*> shortImport = importer.Import(
        u8"draconic_audiopipe_short.wav", *project, *project->SourceDb().RootGroup(), nullptr, nullptr, nullptr);
    REQUIRE(shortImport.HasValue());
    {
        RefPtr<ISerializable> object = shortImport.Value()->ReadObject();
        auto* imported = Cast<AudioClipAsset>(object.Get());
        REQUIRE(imported != nullptr);
        CHECK_FALSE(imported->stream);          // small + short = in-memory
        CHECK(imported->loop);                  // smpl chunk detected
        CHECK(imported->loopStartFrame == 50u);
        CHECK(imported->loopEndFrame == 1900u);
    }

    // An 11-second source crosses the duration line: stream pre-checks on.
    const Array<byte> longWav = MakeToneWav(11.0f, 8000, 1);
    REQUIRE(WriteFile(u8"draconic_audiopipe_long.wav",
                      Span<const byte>(longWav.Data(), longWav.Size())).IsOk());
    Result<content::Instance*> longImport = importer.Import(
        u8"draconic_audiopipe_long.wav", *project, *project->SourceDb().RootGroup(), nullptr, nullptr, nullptr);
    REQUIRE(longImport.HasValue());
    {
        RefPtr<ISerializable> object = longImport.Value()->ReadObject();
        auto* imported = Cast<AudioClipAsset>(object.Get());
        REQUIRE(imported != nullptr);
        CHECK(imported->stream);
    }

    // Garbage is refused before anything lands in the project.
    Array<byte> garbage;
    for (int i = 0; i < 100; ++i) { garbage.PushBack(static_cast<byte>(i)); }
    REQUIRE(WriteFile(u8"draconic_audiopipe_garbage.wav",
                      Span<const byte>(garbage.Data(), garbage.Size())).IsOk());
    CHECK_FALSE(importer.Import(u8"draconic_audiopipe_garbage.wav", *project,
                                *project->SourceDb().RootGroup(), nullptr, nullptr, nullptr).HasValue());

    // Import options object exposes the five toggles.
    RefPtr<draconic::editor::ImportOptions> options = importer.CreateOptions();
    REQUIRE(options.Get() != nullptr);
    CHECK(options->Toggles().Size() == 5u);

    FileDelete(u8"draconic_audiopipe_short.wav");
    FileDelete(u8"draconic_audiopipe_long.wav");
    FileDelete(u8"draconic_audiopipe_garbage.wav");
    FileDelete(PathJoin(projectDir, u8"Sources/draconic_audiopipe_short.wav"));
    FileDelete(PathJoin(projectDir, u8"Sources/draconic_audiopipe_long.wav"));
    FileDelete(PathJoin(projectDir, u8"Content/draconic_audiopipe_short.xasset"));
    FileDelete(PathJoin(projectDir, u8"Content/draconic_audiopipe_long.xasset"));
    cleanProject();
}
