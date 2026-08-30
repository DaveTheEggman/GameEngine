// SPDX-License-Identifier: MIT
// Copyright (c) 2026-Present Robert Campbell

// Audio script FACADE end-to-end: a script drives Audio.playOneShot/playCue/playMusic by content
// path through the resource seam. Cross-layer (scripting x audio), so it lives in Integration, not
// in Engine.Audio.Tests (which stays backend-neutral). Driven on both surviving backends (AngelScript
// + Luau) through a shared fixture; guarded so a build with neither compiles to an empty TU.
#include <doctest/doctest.h>
#include "Core/Prelude.h"
#if defined(OPTION_HAS_ANGELSCRIPT) || defined(OPTION_HAS_LUAU)
#include <cmath>

import foundation.core;
import foundation.audio;
import engine.audio;
import foundation.audio.resource;
import foundation.vfs;
import foundation.content;
import foundation.resource;
import foundation.script;
#ifdef OPTION_HAS_ANGELSCRIPT
import foundation.script.angelscript;
#endif
#ifdef OPTION_HAS_LUAU
import foundation.script.luau;
#endif

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

    // The full facade drive, parameterized by the backend's manager + its two scripts (the main
    // program + the null-binding probe). Shared so AngelScript and Luau prove identical behavior.
    void DriveAudioFacade(RefPtr<foundation::script::IScriptManager> scripts,
                          foundation::core::StringView script,
                          foundation::core::StringView bareScript)
    {
        REQUIRE(scripts.Get() != nullptr);
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

        foundation::script::RegisterReflectedTypes(*scripts);
        RefPtr<foundation::script::IScriptContext> ctx = scripts->CreateContext();
        REQUIRE(ctx.Get() != nullptr);
        subsystem.ExposeToScript(*ctx, &manager);

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
        REQUIRE(bare->Load(bareScript, u8"main").IsOk());
        CHECK_FALSE(bare->GetGlobal(u8"Played").Get<bool>());

        subsystem.Shutdown();
        FileDelete(u8"scratch_audio_scriptdb/sfx/beep.rasset");
        FileDelete(u8"scratch_audio_scriptdb/sfx/beep.data.bin");
        FileDelete(u8"scratch_audio_scriptdb/sfx/steps.rasset");
        RemoveDirectory(u8"scratch_audio_scriptdb/sfx");
        RemoveDirectory(dir);
    }
}

#ifdef OPTION_HAS_ANGELSCRIPT
TEST_CASE("audio-facade: the AngelScript Audio facade plays clips/cues/music by CONTENT PATH "
          "through the resource seam (missing paths no-op, never fault)")
{
    DriveAudioFacade(
        foundation::script::angelscript::CreateScriptManager(),
        u8"bool Played; bool Spatial; bool Cue; bool Music; bool Missing; bool MissingAgain;\n"
        u8"void main() {\n"
        u8"  Played = Audio::playOneShot(\"sfx/beep\");\n"
        u8"  Spatial = Audio::playOneShot3D(\"sfx/beep\", 1.0f, 2.0f, 3.0f);\n"
        u8"  Cue = Audio::playCue(\"sfx/steps\");\n"
        u8"  Music = Audio::playMusic(\"sfx/beep\", 0.1f);\n"
        u8"  Missing = Audio::playOneShot(\"sfx/nope\");\n"
        u8"  MissingAgain = Audio::playOneShot(\"sfx/nope\");\n" // warn-once path
        u8"}\n",
        u8"bool Played; void main() { Played = Audio::playOneShot(\"sfx/beep\"); }\n");
}
#endif // OPTION_HAS_ANGELSCRIPT

#ifdef OPTION_HAS_LUAU
TEST_CASE("audio-facade: the Luau Audio facade plays clips/cues/music by CONTENT PATH "
          "through the resource seam (missing paths no-op, never fault)")
{
    DriveAudioFacade(foundation::script::CreateLuauScriptManager(),
                     u8"Played = Audio.playOneShot(\"sfx/beep\")\n"
                     u8"Spatial = Audio.playOneShot3D(\"sfx/beep\", 1, 2, 3)\n"
                     u8"Cue = Audio.playCue(\"sfx/steps\")\n"
                     u8"Music = Audio.playMusic(\"sfx/beep\", 0.1)\n"
                     u8"Missing = Audio.playOneShot(\"sfx/nope\")\n"
                     u8"MissingAgain = Audio.playOneShot(\"sfx/nope\")\n",
                     u8"Played = Audio.playOneShot(\"sfx/beep\")\n");
}
#endif // OPTION_HAS_LUAU

#endif // OPTION_HAS_ANGELSCRIPT || OPTION_HAS_LUAU
