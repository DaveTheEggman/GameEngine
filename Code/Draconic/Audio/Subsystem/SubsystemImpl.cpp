// Draconic::AudioSubsystem - implementation unit: the per-frame engine drive (listener
// push with active-camera fallback + engine tick) and the component reflection bodies.
// Both live OUTSIDE the interface for GCC: the render import stays out of the
// interface's module graph, and DRACONIC_REFLECT_* bodies in a partition interface
// make GCC emit an unreadable gcm cluster for -fno-module-lazy consumers.

module;
#include "Core/Prelude.h"
#include "Core/Reflection/Reflect.h"

module draconic.audio.subsystem;

import draconic.core;
import draconic.runtime;
import draconic.scene;
import draconic.audio;
import draconic.render.subsystem;   // CameraComponentManager (listener fallback)

using namespace draconic::core;

namespace draconic::audio
{
    namespace
    {
        // Godot behavior: no listener component anywhere = the active camera IS the
        // listener. First enabled camera of the scene wins.
        [[nodiscard]] bool CameraListenerPose(draconic::scene::Scene& scene, Float3& outPosition,
                                              Float3& outForward, Float3& outUp)
        {
            auto* cameras = scene.GetSystem<draconic::render::CameraComponentManager>();
            if (cameras == nullptr) { return false; }
            bool found = false;
            cameras->ForEach([&](draconic::render::CameraComponent&,
                                 draconic::scene::EntityHandle e) {
                if (found) { return; }
                const Float4x4 world = scene.GetWorldMatrix(e);
                outPosition = TransformPoint(Float3{ 0.0f, 0.0f, 0.0f }, world);
                outForward = Normalized(Float3{ -world.m[2][0], -world.m[2][1], -world.m[2][2] });
                outUp = Normalized(Float3{ world.m[1][0], world.m[1][1], world.m[1][2] });
                found = true;
            });
            return found;
        }
    }

    void AudioSubsystem::Update(f32 deltaTime)
    {
        AudioEngine* engine = Engine();
        if (engine == nullptr) { return; }

        // One active listener: the first STARTED scene with a listener COMPONENT wins;
        // otherwise the first started scene's camera stands in.
        bool pushed = false;
        for (const SceneEntry& entry : Systems())
        {
            if (!entry.system->Started()) { continue; }
            if (entry.system->ListenerValid())
            {
                engine->SetListenerTransform(entry.system->ListenerPosition(),
                                             entry.system->ListenerForward(),
                                             entry.system->ListenerUp(),
                                             entry.system->ListenerVelocity());
                pushed = true;
                break;
            }
        }
        if (!pushed)
        {
            for (const SceneEntry& entry : Systems())
            {
                if (!entry.system->Started()) { continue; }
                Float3 position, forward, up;
                if (CameraListenerPose(*entry.scene, position, forward, up))
                {
                    engine->SetListenerTransform(position, forward, up,
                                                 Float3{ 0.0f, 0.0f, 0.0f });
                    break;
                }
            }
        }

        engine->Update(deltaTime);   // reap + dedupe clock (+ headless pump)
    }
}

// ---- reflection (see :components for why this lives here) ----
namespace draconic::audio
{
    DRACONIC_REFLECT_ENUM(AudioBus, "draconic::audio")
    {
        builder.Value("Master", AudioBus::Master);
        builder.Value("Effects", AudioBus::Effects);
        builder.Value("Music", AudioBus::Music);
        builder.Value("UI", AudioBus::UI);
    }

    DRACONIC_REFLECT_ENUM(AudioAttenuationModel, "draconic::audio")
    {
        builder.Value("None", AudioAttenuationModel::None);
        builder.Value("Inverse", AudioAttenuationModel::Inverse);
        builder.Value("Linear", AudioAttenuationModel::Linear);
        builder.Value("Exponential", AudioAttenuationModel::Exponential);
    }

    DRACONIC_REFLECT_VALUE(AudioSourceComponent, "draconic::audio")
    {
        builder.DataVersion(1);
        builder.Property<&AudioSourceComponent::clip>("clip");
        builder.Property<&AudioSourceComponent::bus>("bus");
        builder.Property<&AudioSourceComponent::volume>("volume");
        builder.Property<&AudioSourceComponent::pitch>("pitch");
        builder.Property<&AudioSourceComponent::loop>("loop");
        builder.Property<&AudioSourceComponent::spatial>("spatial");
        builder.Property<&AudioSourceComponent::autoPlay>("autoPlay");
        builder.Property<&AudioSourceComponent::distanceLowpassHz>("distanceLowpassHz");
        builder.Property<&AudioSourceComponent::priority>("priority");
        builder.Property<&AudioSourceComponent::minDistance>("minDistance");
        builder.Property<&AudioSourceComponent::maxDistance>("maxDistance");
        builder.Property<&AudioSourceComponent::attenuationModel>("attenuationModel");
        builder.Property<&AudioSourceComponent::rolloff>("rolloff");
        builder.Property<&AudioSourceComponent::dopplerFactor>("dopplerFactor");
        builder.Property<&AudioSourceComponent::coneInnerAngleDegrees>("coneInnerAngleDegrees");
        builder.Property<&AudioSourceComponent::coneOuterAngleDegrees>("coneOuterAngleDegrees");
        builder.Property<&AudioSourceComponent::coneOuterGain>("coneOuterGain");
    }

    DRACONIC_REFLECT_VALUE(AudioListenerComponent, "draconic::audio")
    {
        builder.DataVersion(1);
        builder.Property<&AudioListenerComponent::isActive>("isActive");
    }

    DRACONIC_DEFINE_OBJECT(AudioUserSettings, "draconic::audio")

    DRACONIC_REFLECT(Audio, "draconic::audio")
    {
        builder.Method<&Audio::setBusVolume>("setBusVolume");
        builder.Method<&Audio::busVolume>("busVolume");
        builder.Method<&Audio::setBusMuted>("setBusMuted");
        builder.Method<&Audio::busMuted>("busMuted");
        builder.Method<&Audio::stopMusic>("stopMusic");
        builder.Constructor();   // Wren only materializes constructible foreign classes
    }

    void RegisterAudioScriptApi()
    {
        GlobalTypeRegistry().Register(Audio::StaticType());
    }

    void RegisterAudioComponentReflection()
    {
        static const bool once = []() {
            DraconicRegisterEnum_AudioBus();
            DraconicRegisterEnum_AudioAttenuationModel();
            DraconicRegisterValue_AudioSourceComponent();
            DraconicRegisterValue_AudioListenerComponent();
            return true;
        }();
        (void)once;
    }
}
