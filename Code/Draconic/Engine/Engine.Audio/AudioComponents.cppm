// Engine::Audio - :components partition.
//
// The authoring components (docs/design/audio.md §6): AudioSourceComponent carries the
// clip reference + playback/3D intent; AudioListenerComponent selects the listener
// entity (first active wins; no listener = the subsystem falls back to the active
// camera's transform, Godot behavior). Runtime fields (voice handle, previous-frame
// position for velocity/doppler) are transient - never serialized.

module;
#include "Core/Prelude.h"

export module engine.audio:components;

import foundation.core;
import foundation.scene;
import foundation.resource;
import foundation.audio;

using namespace foundation::core;
using namespace foundation::audio;

export namespace engine::audio
{
    struct AudioSourceComponent
    {
        // Authored:
        foundation::resource::Ref<AudioClip> clip;
        // Optional cue: when set it WINS over `clip` - each Play trigger resolves a
        // weighted variant with the cue's jitter (autoplay/loop apply to the pick).
        foundation::resource::Ref<SoundCue> cue;
        AudioBus bus = AudioBus::Effects;
        // Named custom-bus routing (v2): when non-empty and the applied layout has a
        // custom bus of this name, the voice routes there; unknown/empty = `bus`.
        String busName;
        f32 volume = 1.0f;
        f32 pitch = 1.0f;  // real resampling at runtime
        bool loop = false; // OR-ed with the clip's authored loop intent
        bool spatial = true;
        bool autoPlay = false; // starts when scene simulation starts
        // Distance low-pass floor (Hz) for spatial sources: the cutoff glides from
        // open at minDistance to this at maxDistance. 0 = no muffling filter.
        f32 distanceLowpassHz = 4000.0f;
        // Per-voice reverb send (v3): 0..1 scaling this source's feed into the
        // scene's send reverb (zones drive the room character). 0 = dry only.
        f32 reverbSend = 0.0f;
        u8 priority = 128; // pool contention (higher survives)
        f32 minDistance = 1.0f;
        f32 maxDistance = 100.0f;
        AudioAttenuationModel attenuationModel = AudioAttenuationModel::Inverse;
        f32 rolloff = 1.0f;
        f32 dopplerFactor = 1.0f; // velocities feed the spatializer per frame
        f32 coneInnerAngleDegrees = 360.0f;
        f32 coneOuterAngleDegrees = 360.0f;
        f32 coneOuterGain = 0.0f;

        // Runtime (transient):
        VoiceHandle voice;
        i32 lastCueVariant = -1; // cue no-repeat state (runtime)
        u32 cueSequentialCursor = 0;
        Float3 previousPosition{0.0f, 0.0f, 0.0f};
        bool hasPreviousPosition = false;
    };

    inline void Serialize(ISerializer& ar, AudioSourceComponent& c)
    {
        foundation::core::Serialize(ar, "clip", c.clip);
        u8 bus = static_cast<u8>(c.bus);
        u8 attenuation = static_cast<u8>(c.attenuationModel);
        foundation::core::Serialize(ar, "bus", bus);
        c.bus = static_cast<AudioBus>(bus);
        foundation::core::Serialize(ar, "volume", c.volume);
        foundation::core::Serialize(ar, "pitch", c.pitch);
        foundation::core::Serialize(ar, "loop", c.loop);
        foundation::core::Serialize(ar, "spatial", c.spatial);
        foundation::core::Serialize(ar, "autoPlay", c.autoPlay);
        foundation::core::Serialize(ar, "distanceLowpassHz", c.distanceLowpassHz);
        foundation::core::Serialize(ar, "cue", c.cue);
        foundation::core::Serialize(ar, "priority", c.priority);
        foundation::core::Serialize(ar, "minDistance", c.minDistance);
        foundation::core::Serialize(ar, "maxDistance", c.maxDistance);
        foundation::core::Serialize(ar, "attenuationModel", attenuation);
        c.attenuationModel = static_cast<AudioAttenuationModel>(attenuation);
        foundation::core::Serialize(ar, "rolloff", c.rolloff);
        foundation::core::Serialize(ar, "dopplerFactor", c.dopplerFactor);
        foundation::core::Serialize(ar, "coneInnerAngleDegrees", c.coneInnerAngleDegrees);
        foundation::core::Serialize(ar, "coneOuterAngleDegrees", c.coneOuterAngleDegrees);
        foundation::core::Serialize(ar, "coneOuterGain", c.coneOuterGain);
        if (ar.Version() >= 2) // v2: named custom-bus routing
        {
            foundation::core::Serialize(ar, "busName", c.busName);
        }
        if (ar.Version() >= 3) // v3: per-voice reverb send
        {
            foundation::core::Serialize(ar, "reverbSend", c.reverbSend);
        }
    }

    inline void ResolveResources(foundation::resource::ResourceManager& manager,
                                 AudioSourceComponent& c)
    {
        c.clip.Bind(manager);
        c.cue.Bind(manager);
    }

    class AudioSourceComponentManager final
        : public foundation::scene::SerializableComponentManager<AudioSourceComponent>
    {
    public:
        AudioSourceComponentManager()
            : SerializableComponentManager<AudioSourceComponent>(u8"audio.Source")
        {
        }
    };

    // First ACTIVE listener wins; entities beyond the first are ignored that frame.
    struct AudioListenerComponent
    {
        bool isActive = true;

        // Runtime (transient):
        Float3 previousPosition{0.0f, 0.0f, 0.0f};
        bool hasPreviousPosition = false;
    };

    inline void Serialize(ISerializer& ar, AudioListenerComponent& c)
    {
        foundation::core::Serialize(ar, "isActive", c.isActive);
    }

    // ---- reverb zones (P3): environmental reverb follows the LISTENER ----
    // A sphere volume; when the scene's listener is inside, the scene's Effects tier
    // reverberates - wet fades in across the edge band, the WETTEST zone wins.
    struct AudioReverbZoneComponent
    {
        f32 radius = 8.0f;
        f32 edgeFade = 0.25f; // fraction of the radius that fades wet 0 -> full
        f32 roomSize = 0.6f;
        f32 damping = 0.4f;
        f32 wetLevel = 0.5f;
        bool enabled = true;
    };

    inline void Serialize(ISerializer& ar, AudioReverbZoneComponent& c)
    {
        foundation::core::Serialize(ar, "radius", c.radius);
        foundation::core::Serialize(ar, "edgeFade", c.edgeFade);
        foundation::core::Serialize(ar, "roomSize", c.roomSize);
        foundation::core::Serialize(ar, "damping", c.damping);
        foundation::core::Serialize(ar, "wetLevel", c.wetLevel);
        foundation::core::Serialize(ar, "enabled", c.enabled);
    }

    class AudioReverbZoneComponentManager final
        : public foundation::scene::SerializableComponentManager<AudioReverbZoneComponent>
    {
    public:
        AudioReverbZoneComponentManager()
            : SerializableComponentManager<AudioReverbZoneComponent>(u8"audio.ReverbZone")
        {
        }
    };

    class AudioListenerComponentManager final
        : public foundation::scene::SerializableComponentManager<AudioListenerComponent>
    {
    public:
        AudioListenerComponentManager()
            : SerializableComponentManager<AudioListenerComponent>(u8"audio.Listener")
        {
        }
    };

    // Defined in SubsystemImpl.cpp: the DRACONIC_REFLECT_* bodies live there because
    // GCC's module serializer emits an unreadable gcm cluster when they sit in a
    // partition interface (the -fno-module-lazy eager load then fails for consumers).
    void RegisterAudioComponentReflection();
}
