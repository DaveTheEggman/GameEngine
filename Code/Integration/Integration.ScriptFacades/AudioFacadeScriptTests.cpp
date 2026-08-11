// Audio script FACADE end-to-end: a script drives Audio.playOneShot/playCue/playMusic by content
// path through the resource seam. Cross-layer (scripting x audio), so it lives in Integration, not
// in Engine.Audio.Tests (which stays backend-neutral). Wren-only today; guarded so a Wren-less build
// compiles to an empty TU.
#include <doctest/doctest.h>
#include "Core/Prelude.h"
#ifdef OPTION_HAS_WREN
#include <cmath>

import foundation.core;
import foundation.audio;
import engine.audio;
import foundation.audio.resource;
import foundation.vfs;
import foundation.content;
import foundation.resource;
import foundation.script;
import foundation.script.wren;

using namespace foundation::core;
using namespace engine::audio;
using namespace foundation::audio;

namespace
{
    [[nodiscard]] RefPtr<AudioClip> MakeToneClip(f32 seconds, u32 sampleRate = 8000, u32 channels = 1)
    {
        Array<i16> samples;
        const usize frameCount = static_cast<usize>(seconds * static_cast<f32>(sampleRate));
        for (usize frame = 0; frame < frameCount; ++frame)
        {
            const f32 t = static_cast<f32>(frame) / static_cast<f32>(sampleRate);
            const i16 sample =
                static_cast<i16>(0.5f * std::sin(2.0f * 3.14159265f * 440.0f * t) * 32000.0f);
            for (u32 channel = 0; channel < channels; ++channel)
            {
                samples.PushBack(sample);
            }
        }
        Array<byte> wav;
        REQUIRE(EncodeWavFromPcm16(Span<const i16>(samples.Data(), samples.Size()), channels,
                                   sampleRate, wav));
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
}

TEST_CASE("audio-facade: the Wren Audio facade plays clips/cues/music by CONTENT PATH "
          "through the resource seam (missing paths no-op, never fault)")
{
    RegisterAudioScriptFacade();
    RegisterAudioResource();

    // A hand-cooked content DB: sfx/beep (clip) + sfx/steps (cue referencing it).
    const StringView dir = u8"scratch_audio_scriptdb";
    FileDelete(u8"scratch_audio_scriptdb/sfx/beep.rasset");
    FileDelete(u8"scratch_audio_scriptdb/sfx/beep.data.bin");
    FileDelete(u8"scratch_audio_scriptdb/sfx/steps.rasset");
    RemoveDirectory(u8"scratch_audio_scriptdb/sfx");
    RemoveDirectory(dir);
    foundation::vfs::NativeFileSystem mount(dir);
    foundation::content::ContentDatabase db(mount, BinarySerializerFactory(), u8".rasset");
    foundation::content::Group* sfx = db.RootGroup()->CreateGroup(u8"sfx");
    REQUIRE(sfx != nullptr);

    RefPtr<AudioClip> tone = MakeToneClip(0.5f);
    auto* beep = sfx->CreateInstance(u8"beep", AudioClipSource::StaticType());
    REQUIRE(beep != nullptr);
    AudioClipSource clipRecord;
    clipRecord.channels = tone->channels;
    clipRecord.sampleRate = tone->sampleRate;
    clipRecord.frameCount = tone->frameCount;
    clipRecord.durationSeconds = tone->durationSeconds;
    REQUIRE(beep->WriteObject(clipRecord).IsOk());
    REQUIRE(beep->WriteData(u8"data",
                            Span<const byte>(tone->encodedData.Data(), tone->encodedData.Size()))
                .IsOk());

    auto* steps = sfx->CreateInstance(u8"steps", SoundCueSource::StaticType());
    REQUIRE(steps != nullptr);
    SoundCueSource cueRecord;
    SoundCueSource::Variant variant;
    variant.clipId = beep->Id();
    variant.weight = 1.0f;
    cueRecord.variants.PushBack(variant);
    REQUIRE(steps->WriteObject(cueRecord).IsOk());

    AudioClipFactory clipFactory;
    SoundCueFactory cueFactory;
    foundation::resource::ResourceManager manager(db);
    manager.AddFactory(&clipFactory);
    manager.AddFactory(&cueFactory);

    // A headless subsystem (Init is the public lifecycle seam) + the script binding.
    AudioEngineSettings engineSettings;
    engineSettings.headless = true;
    engineSettings.dedupeWindowSeconds = 0.0f; // each facade call = its own voice
    AudioSubsystem subsystem(engineSettings);
    subsystem.Init();
    REQUIRE(subsystem.Engine() != nullptr);

    RefPtr<foundation::script::IScriptManager> scripts =
        foundation::script::wren::CreateScriptManager();
    foundation::script::RegisterReflectedTypes(*scripts);
    RefPtr<foundation::script::IScriptContext> ctx = scripts->CreateContext();
    REQUIRE(ctx.Get() != nullptr);
    subsystem.ExposeToScript(*ctx, &manager);

    const StringView script =
        u8"var Played = Audio.playOneShot(\"sfx/beep\")\n"
        u8"var Spatial = Audio.playOneShot3D(\"sfx/beep\", 1, 2, 3)\n"
        u8"var Cue = Audio.playCue(\"sfx/steps\")\n"
        u8"var Music = Audio.playMusic(\"sfx/beep\", 0.1)\n"
        u8"var Missing = Audio.playOneShot(\"sfx/nope\")\n"
        u8"var MissingAgain = Audio.playOneShot(\"sfx/nope\")\n"; // warn-once path
    REQUIRE(ctx->Load(script, u8"main").IsOk());
    CHECK(ctx->GetGlobal(u8"Played").Get<bool>());
    CHECK(ctx->GetGlobal(u8"Spatial").Get<bool>());
    CHECK(ctx->GetGlobal(u8"Cue").Get<bool>());
    CHECK(ctx->GetGlobal(u8"Music").Get<bool>());
    CHECK_FALSE(ctx->GetGlobal(u8"Missing").Get<bool>());
    CHECK_FALSE(ctx->GetGlobal(u8"MissingAgain").Get<bool>());
    CHECK(subsystem.Engine()->ActiveVoiceCount() == 4u);
    VoiceStatus music;
    REQUIRE(subsystem.Engine()->GetVoiceStatus(subsystem.Engine()->MusicVoice(), music));
    CHECK(music.bus == AudioBus::Music);

    // No binding bound: playback calls report false, never a fault.
    RefPtr<foundation::script::IScriptContext> bare = scripts->CreateContext();
    REQUIRE(bare->Load(u8"var Played = Audio.playOneShot(\"sfx/beep\")\n", u8"main").IsOk());
    CHECK_FALSE(bare->GetGlobal(u8"Played").Get<bool>());

    subsystem.Shutdown();
    FileDelete(u8"scratch_audio_scriptdb/sfx/beep.rasset");
    FileDelete(u8"scratch_audio_scriptdb/sfx/beep.data.bin");
    FileDelete(u8"scratch_audio_scriptdb/sfx/steps.rasset");
    RemoveDirectory(u8"scratch_audio_scriptdb/sfx");
    RemoveDirectory(dir);
}
#endif // OPTION_HAS_WREN
