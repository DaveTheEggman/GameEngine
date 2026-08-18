// Engine::Audio - implementation unit: the per-frame engine drive (listener
// push with active-camera fallback + engine tick) and the component reflection bodies.
// Both live OUTSIDE the interface for GCC: the render import stays out of the
// interface's module graph, and REFLECT_* bodies in a partition interface
// make GCC emit an unreadable gcm cluster for -fno-module-lazy consumers.

module;
#include "Core/Prelude.h"
#include "Core/Reflection/Reflect.h"
#include "Profiler/Profiler.h"

module engine.audio;

import foundation.core;
import foundation.profiler;
import foundation.runtime;
import foundation.scene;
import foundation.audio;
import foundation.materials;
import engine.render; // CameraComponentManager (listener fallback)
import foundation.script.facades; // RegisterExtraFacadeName (Audio into the behavior prelude)

using namespace foundation::core;
using namespace foundation::audio;
namespace core = foundation::core;

namespace engine::audio
{
    namespace
    {
        // Godot behavior: no listener component anywhere = the active camera IS the
        // listener. First enabled camera of the scene wins.
        [[nodiscard]] bool CameraListenerPose(foundation::scene::Scene& scene, Float3& outPosition,
                                              Float3& outForward, Float3& outUp)
        {
            auto* cameras = scene.GetSystem<engine::render::CameraComponentManager>();
            if (cameras == nullptr)
            {
                return false;
            }
            bool found = false;
            cameras->ForEach(
                [&](engine::render::CameraComponent&, foundation::scene::EntityHandle e)
                {
                    if (found)
                    {
                        return;
                    }
                    const Float4x4 world = scene.GetWorldMatrix(e);
                    outPosition = TransformPoint(Float3{0.0f, 0.0f, 0.0f}, world);
                    outForward = Normalized(Float3{-world.m[2][0], -world.m[2][1], -world.m[2][2]});
                    outUp = Normalized(Float3{world.m[1][0], world.m[1][1], world.m[1][2]});
                    found = true;
                });
            return found;
        }
    }

    void AudioSubsystem::Update(f32 deltaTime)
    {
        AudioEngine* engine = Engine();
        if (engine == nullptr)
        {
            return;
        }
        PROFILE_SCOPE("Audio.Engine");

        // Listeners (multi-listener, P3): every active listener COMPONENT across the
        // started scenes fills an engine listener slot, in scene order, up to the
        // engine's configured count - spatial voices attenuate against the CLOSEST
        // enabled listener (split-screen ears). No component anywhere = the first
        // started scene's camera stands in on slot 0. Unused slots disable.
        const u32 capacity = engine->ListenerCount();
        u32 used = 0;
        for (const SceneEntry& entry : Systems())
        {
            if (!entry.system->Started() || used >= capacity)
            {
                continue;
            }
            for (const ListenerPose& pose : entry.system->ListenerPoses())
            {
                if (used >= capacity)
                {
                    break;
                }
                engine->SetListenerTransformIndexed(used, pose.position, pose.forward, pose.up,
                                                    pose.velocity);
                engine->SetListenerEnabled(used, true);
                ++used;
            }
        }
        if (used == 0)
        {
            for (const SceneEntry& entry : Systems())
            {
                if (!entry.system->Started())
                {
                    continue;
                }
                Float3 position, forward, up;
                if (CameraListenerPose(*entry.scene, position, forward, up))
                {
                    engine->SetListenerTransformIndexed(0, position, forward, up,
                                                        Float3{0.0f, 0.0f, 0.0f});
                    engine->SetListenerEnabled(0, true);
                    used = 1;
                    break;
                }
            }
        }
        for (u32 i = Max(used, 1u); i < capacity; ++i) // slot 0 always stays enabled
        {
            engine->SetListenerEnabled(i, false);
        }

        engine->Update(deltaTime); // reap + dedupe clock (+ headless pump)
    }
}

// ---- reflection (see :components for why this lives here) ----
namespace engine::audio
{
    REFLECT_ENUM(AudioBus, "rtti::engine::audio")
    {
        builder.Value("Master", AudioBus::Master);
        builder.Value("Effects", AudioBus::Effects);
        builder.Value("Music", AudioBus::Music);
        builder.Value("UI", AudioBus::UI);
    }

    REFLECT_ENUM(AudioAttenuationModel, "rtti::engine::audio")
    {
        builder.Value("None", AudioAttenuationModel::None);
        builder.Value("Inverse", AudioAttenuationModel::Inverse);
        builder.Value("Linear", AudioAttenuationModel::Linear);
        builder.Value("Exponential", AudioAttenuationModel::Exponential);
    }

    REFLECT_ENUM(AudioSourceType, "rtti::engine::audio")
    {
        builder.Value("Clip", AudioSourceType::Clip);
        builder.Value("Cue", AudioSourceType::Cue);
    }

    REFLECT_VALUE(AudioSourceComponent, "rtti::engine::audio")
    {
        // v4: sourceType discriminant (Clip/Cue) drives the inspector + runtime.
        builder.Attribute("displayName", String(u8"Audio Source"))
            .Attribute("category", String(u8"Audio")).DataVersion(4);
        // Script (Track A): AudioSourceComponent.of(entity) -> live volume/pitch/loop/spatial/etc.
        // play/stop/pause + clip swap are engine ops -> SceneAudio.of(scene) (world ops keyed by entity).
        builder.Method<&foundation::script::ComponentOf<AudioSourceComponent>, AudioSourceComponent>(
            "of");
        // Source: pick Clip or Cue; only the chosen Ref shows (visibleWhen keys on sourceType).
        builder.Property<&AudioSourceComponent::sourceType>("sourceType");
        builder.Property<&AudioSourceComponent::clip>("clip").PropAttribute(
            "visibleWhen", String(u8"sourceType=0"));
        builder.Property<&AudioSourceComponent::cue>("cue").PropAttribute("visibleWhen",
                                                                          String(u8"sourceType=1"));
        builder.Property<&AudioSourceComponent::bus>("bus");
        builder.Property<&AudioSourceComponent::busName>("busName");
        builder.Property<&AudioSourceComponent::volume>("volume");
        builder.Property<&AudioSourceComponent::pitch>("pitch");
        builder.Property<&AudioSourceComponent::loop>("loop");
        builder.Property<&AudioSourceComponent::autoPlay>("autoPlay");
        builder.Property<&AudioSourceComponent::priority>("priority");
        builder.Property<&AudioSourceComponent::reverbSend>("reverbSend");
        // Spatial-only fields: hidden for a non-spatial (2D) source.
        builder.Property<&AudioSourceComponent::spatial>("spatial");
        builder.Property<&AudioSourceComponent::distanceLowpassHz>("distanceLowpassHz")
            .PropAttribute("visibleWhen", String(u8"spatial"));
        builder.Property<&AudioSourceComponent::minDistance>("minDistance")
            .PropAttribute("visibleWhen", String(u8"spatial"));
        builder.Property<&AudioSourceComponent::maxDistance>("maxDistance")
            .PropAttribute("visibleWhen", String(u8"spatial"));
        builder.Property<&AudioSourceComponent::attenuationModel>("attenuationModel")
            .PropAttribute("visibleWhen", String(u8"spatial"));
        builder.Property<&AudioSourceComponent::rolloff>("rolloff").PropAttribute(
            "visibleWhen", String(u8"spatial"));
        builder.Property<&AudioSourceComponent::dopplerFactor>("dopplerFactor")
            .PropAttribute("visibleWhen", String(u8"spatial"));
        builder.Property<&AudioSourceComponent::coneInnerAngleDegrees>("coneInnerAngleDegrees")
            .PropAttribute("visibleWhen", String(u8"spatial"));
        builder.Property<&AudioSourceComponent::coneOuterAngleDegrees>("coneOuterAngleDegrees")
            .PropAttribute("visibleWhen", String(u8"spatial"));
        builder.Property<&AudioSourceComponent::coneOuterGain>("coneOuterGain")
            .PropAttribute("visibleWhen", String(u8"spatial"));
    }

    REFLECT_VALUE(AudioListenerComponent, "rtti::engine::audio")
    {
        builder.Attribute("displayName", String(u8"Audio Listener"))
            .Attribute("category", String(u8"Audio")).DataVersion(1);
        builder.Property<&AudioListenerComponent::isActive>("isActive");
    }

    RTTI_DEFINE_OBJECT(AudioUserSettings, "rtti::engine::audio")

    REFLECT_MEMBERS(Audio, "rtti::engine::audio")
    {
        builder.Method<&Audio::setBusVolume>("setBusVolume");
        builder.Method<&Audio::busVolume>("busVolume");
        builder.Method<&Audio::setBusMuted>("setBusMuted");
        builder.Method<&Audio::busMuted>("busMuted");
        builder.Method<&Audio::stopMusic>("stopMusic");
        // Content-path playback (item 4): path = the editor's source-DB content path.
        builder.Method<&Audio::playOneShot>("playOneShot");
        builder.Method<&Audio::playOneShot3D>("playOneShot3D");
        builder.Method<&Audio::playCue>("playCue");
        builder.Method<&Audio::playMusic>("playMusic");
        builder.Constructor(); // Wren only materializes constructible foreign classes
    }

    // The scene-bound audio handle: SceneAudio.of(scene).play(entity) / stop / pause / isPlaying /
    // setClip(entity, id). `of` returns SceneAudio by value (concrete cross-backend return), like
    // ScenePhysics. All world ops keyed by entity - they reach the scene's AudioEngine.
    REFLECT_VALUE(SceneAudio, "rtti::engine::audio")
    {
        builder.Method<&SceneAudio::play>("play", {"entity"});
        builder.Method<&SceneAudio::stop>("stop", {"entity"});
        builder.Method<&SceneAudio::pause>("pause", {"entity", "paused"});
        builder.Method<&SceneAudio::isPlaying>("isPlaying", {"entity"});
        builder.Method<&SceneAudio::setClip>("setClip", {"entity", "resourceId"});
        builder.Method<&SceneAudio::of>("of", {"scene"});
        builder.Constructor(); // Wren only materializes constructible foreign classes
    }

    void RegisterAudioScriptFacade()
    {
        RegisterAudioComponentReflection(); // ensure component TypeData (incl `of`) is built first
        GlobalTypeRegistry().Register(Audio::StaticType());
        // So the Wren behavior/Level prelude imports `Audio` too (AngelScript binds by
        // registry). Without this only top-level `main`/Game scripts can see it. Idempotent.
        foundation::script::RegisterExtraFacadeName(u8"Audio");

        // Surface the audio SOURCE component to script (AudioSourceComponent.of(entity) - live
        // volume/pitch/loop/...): register it, seed the Wren emission root, name it for the prelude.
        GlobalTypeRegistry().Register(core::TypeOf<AudioSourceComponent>());
        foundation::script::RegisterExtraScriptRootType(&core::TypeOf<AudioSourceComponent>());
        foundation::script::RegisterExtraFacadeName(u8"AudioSourceComponent");

        // The scene-bound audio handle (SceneAudio.of(scene)): reflect it, register + seed + name it.
        RttiRegisterValue_SceneAudio();
        GlobalTypeRegistry().Register(core::TypeOf<SceneAudio>());
        foundation::script::RegisterExtraScriptRootType(&core::TypeOf<SceneAudio>());
        foundation::script::RegisterExtraFacadeName(u8"SceneAudio");
    }

    REFLECT_VALUE(AudioReverbZoneComponent, "rtti::engine::audio")
    {
        builder.Attribute("displayName", String(u8"Reverb Zone"))
            .Attribute("category", String(u8"Audio")).DataVersion(1);
        builder.Property<&AudioReverbZoneComponent::radius>("radius");
        builder.Property<&AudioReverbZoneComponent::edgeFade>("edgeFade");
        builder.Property<&AudioReverbZoneComponent::roomSize>("roomSize");
        builder.Property<&AudioReverbZoneComponent::damping>("damping");
        builder.Property<&AudioReverbZoneComponent::wetLevel>("wetLevel");
        builder.Property<&AudioReverbZoneComponent::enabled>("enabled");
    }

    void RegisterAudioComponentReflection()
    {
        static const bool once = []()
        {
            RttiRegisterEnum_AudioBus();
            RttiRegisterEnum_AudioAttenuationModel();
            RttiRegisterEnum_AudioSourceType();
            RttiRegisterValue_AudioSourceComponent();
            RttiRegisterValue_AudioListenerComponent();
            RttiRegisterValue_AudioReverbZoneComponent();
            return true;
        }();
        (void)once;
    }
}
