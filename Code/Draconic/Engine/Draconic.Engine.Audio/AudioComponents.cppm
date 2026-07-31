// Draconic::AudioSubsystem - :components partition.
//
// The authoring components (docs/design/audio.md §6): AudioSourceComponent carries the
// clip reference + playback/3D intent; AudioListenerComponent selects the listener
// entity (first active wins; no listener = the subsystem falls back to the active
// camera's transform, Godot behavior). Runtime fields (voice handle, previous-frame
// position for velocity/doppler) are transient - never serialized.

module;
#include "Draconic.Core/Prelude.h"

export module draconic.engine.audio:components;

import draconic.core;
import draconic.scene;
import draconic.resource;
import draconic.audio;

using namespace draconic::core;

export namespace draconic::audio
{
    struct AudioSourceComponent
    {
        // Authored:
        draconic::resource::Ref<AudioClip> clip;
        // Optional cue: when set it WINS over `clip` - each Play trigger resolves a
        // weighted variant with the cue's jitter (autoplay/loop apply to the pick).
        draconic::resource::Ref<SoundCue> cue;
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
        draconic::core::Serialize(ar, "clip", c.clip);
        u8 bus = static_cast<u8>(c.bus);
        u8 attenuation = static_cast<u8>(c.attenuationModel);
        draconic::core::Serialize(ar, "bus", bus);
        c.bus = static_cast<AudioBus>(bus);
        draconic::core::Serialize(ar, "volume", c.volume);
        draconic::core::Serialize(ar, "pitch", c.pitch);
        draconic::core::Serialize(ar, "loop", c.loop);
        draconic::core::Serialize(ar, "spatial", c.spatial);
        draconic::core::Serialize(ar, "autoPlay", c.autoPlay);
        draconic::core::Serialize(ar, "distanceLowpassHz", c.distanceLowpassHz);
        draconic::core::Serialize(ar, "cue", c.cue);
        draconic::core::Serialize(ar, "priority", c.priority);
        draconic::core::Serialize(ar, "minDistance", c.minDistance);
        draconic::core::Serialize(ar, "maxDistance", c.maxDistance);
        draconic::core::Serialize(ar, "attenuationModel", attenuation);
        c.attenuationModel = static_cast<AudioAttenuationModel>(attenuation);
        draconic::core::Serialize(ar, "rolloff", c.rolloff);
        draconic::core::Serialize(ar, "dopplerFactor", c.dopplerFactor);
        draconic::core::Serialize(ar, "coneInnerAngleDegrees", c.coneInnerAngleDegrees);
        draconic::core::Serialize(ar, "coneOuterAngleDegrees", c.coneOuterAngleDegrees);
        draconic::core::Serialize(ar, "coneOuterGain", c.coneOuterGain);
        if (ar.Version() >= 2) // v2: named custom-bus routing
        {
            draconic::core::Serialize(ar, "busName", c.busName);
        }
        if (ar.Version() >= 3) // v3: per-voice reverb send
        {
            draconic::core::Serialize(ar, "reverbSend", c.reverbSend);
        }
    }

    inline void ResolveResources(draconic::resource::ResourceManager& manager,
                                 AudioSourceComponent& c)
    {
        c.clip.Bind(manager);
        c.cue.Bind(manager);
    }

    class AudioSourceComponentManager final
        : public draconic::scene::SerializableComponentManager<AudioSourceComponent>
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
        draconic::core::Serialize(ar, "isActive", c.isActive);
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
        draconic::core::Serialize(ar, "radius", c.radius);
        draconic::core::Serialize(ar, "edgeFade", c.edgeFade);
        draconic::core::Serialize(ar, "roomSize", c.roomSize);
        draconic::core::Serialize(ar, "damping", c.damping);
        draconic::core::Serialize(ar, "wetLevel", c.wetLevel);
        draconic::core::Serialize(ar, "enabled", c.enabled);
    }

    class AudioReverbZoneComponentManager final
        : public draconic::scene::SerializableComponentManager<AudioReverbZoneComponent>
    {
    public:
        AudioReverbZoneComponentManager()
            : SerializableComponentManager<AudioReverbZoneComponent>(u8"audio.ReverbZone")
        {
        }
    };

    class AudioListenerComponentManager final
        : public draconic::scene::SerializableComponentManager<AudioListenerComponent>
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
