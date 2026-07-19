// Draconic::AudioSubsystem - the `draconic.audio.subsystem` module.
//
// Scene integration (docs/design/audio.md §6): an AudioSceneSystem per scene owns a
// per-scene voice group (open question 1: YES - pause/stop-all per scene falls out of
// the engine's group graph naturally, which play-in-editor needs). The PostTransform
// tick resolves clip refs, autoplays on simulation start, syncs position + velocity
// (previous-frame delta - the doppler feed) to live voices, reaps finished one-shots,
// and computes the scene's listener pose from the first active AudioListenerComponent.
// The subsystem owns the ONE AudioEngine, pushes the winning listener (component first,
// active camera fallback - camera contact lives in SubsystemImpl.cpp, the render import
// stays out of this interface), and exposes the engine-global one-shot API.

module;
#include "Core/Prelude.h"
#include "Core/Log/Log.h"

export module draconic.audio.subsystem;

export import :components;

import draconic.core;
import draconic.runtime;
import draconic.scene;
import draconic.scene.subsystem;
import draconic.audio;
// NOTE: no render imports HERE - the camera-fallback listener lives in SubsystemImpl.cpp
// (a module implementation unit), keeping heavyweight imports out of the interface for
// GCC's -fno-module-lazy consumers.

using namespace draconic::core;

export namespace draconic::audio
{
    namespace dscene = draconic::scene;

    class AudioSceneSystem final : public dscene::SceneSystem
    {
    public:
        void OnSceneCreate(dscene::Scene& scene) override { m_scene = &scene; }

        /// The subsystem wires its engine in right after AddSystem (tests may inject a
        /// headless engine directly).
        void SetEngine(AudioEngine* engine) noexcept { m_engine = engine; }
        [[nodiscard]] AudioEngine* Engine() const noexcept { return m_engine; }
        [[nodiscard]] u64 SceneGroup() const noexcept { return m_sceneGroup; }
        [[nodiscard]] dscene::Scene* ScenePtr() const noexcept { return m_scene; }
        [[nodiscard]] bool Started() const noexcept { return m_started; }

        // ---- play lifecycle ----

        void OnSceneStarted() override
        {
            m_started = true;
            m_wasSimulating = true;
            if (m_engine == nullptr) { return; }
            m_sceneGroup = m_engine->CreateSceneGroup();

            // Autoplay: world matrices are current before this fires (Scene::Start
            // guarantee) - positional voices start where they were authored.
            if (auto* sources = m_scene->GetSystem<AudioSourceComponentManager>())
            {
                sources->ForEach([&](AudioSourceComponent& c, dscene::EntityHandle e) {
                    if (c.autoPlay) { PlayComponent(c, e); }
                });
            }
        }

        void OnSceneStopped() override
        {
            m_started = false;
            if (auto* sources = m_scene->GetSystem<AudioSourceComponentManager>())
            {
                sources->ForEach([](AudioSourceComponent& c, dscene::EntityHandle) {
                    c.voice = VoiceHandle{};
                    c.hasPreviousPosition = false;
                });
            }
            if (m_engine != nullptr && m_sceneGroup != 0)
            {
                m_engine->DestroySceneGroup(m_sceneGroup);   // stops the scene's voices
            }
            m_sceneGroup = 0;
            m_listenerValid = false;
        }

        // ---- component control surface (the runtime controls Sedulous never had) ----

        /// Starts (or restarts) the entity's source. Returns the voice handle.
        VoiceHandle Play(dscene::EntityHandle entity)
        {
            auto* sources = m_scene != nullptr
                ? m_scene->GetSystem<AudioSourceComponentManager>() : nullptr;
            AudioSourceComponent* component =
                sources != nullptr ? sources->Get(entity) : nullptr;
            if (component == nullptr) { return {}; }
            return PlayComponent(*component, entity);
        }

        void Stop(dscene::EntityHandle entity)
        {
            if (AudioSourceComponent* component = Component(entity))
            {
                if (m_engine != nullptr) { m_engine->Stop(component->voice); }
                component->voice = VoiceHandle{};
                component->hasPreviousPosition = false;
            }
        }

        void SetPaused(dscene::EntityHandle entity, bool paused)
        {
            if (AudioSourceComponent* component = Component(entity))
            {
                if (m_engine != nullptr) { m_engine->SetPaused(component->voice, paused); }
            }
        }

        [[nodiscard]] bool IsPlaying(dscene::EntityHandle entity)
        {
            AudioSourceComponent* component = Component(entity);
            return component != nullptr && m_engine != nullptr
                && m_engine->IsPlaying(component->voice);
        }

        // ---- per-frame sync (PostTransform: final transforms are ready) ----

        void OnUpdate(dscene::ScenePhase phase, f32 deltaTime) override
        {
            if (phase != dscene::ScenePhase::PostTransform) { return; }
            if (m_engine == nullptr || m_scene == nullptr || !m_started) { return; }

            // Scene simulation pause/resume maps onto the per-scene group (fade both ways).
            const bool simulating = m_scene->SimulationEnabled();
            if (simulating != m_wasSimulating && m_sceneGroup != 0)
            {
                m_engine->SetSceneGroupPaused(m_sceneGroup, !simulating);
            }
            m_wasSimulating = simulating;
            if (!simulating) { return; }

            if (auto* sources = m_scene->GetSystem<AudioSourceComponentManager>())
            {
                sources->ForEach([&](AudioSourceComponent& c, dscene::EntityHandle e) {
                    if (!c.voice.IsValid()) { return; }
                    if (!m_engine->IsValidHandle(c.voice))
                    {
                        c.voice = VoiceHandle{};        // finished one-shot: reap the handle
                        c.hasPreviousPosition = false;
                        return;
                    }
                    if (!c.spatial) { return; }
                    const Float3 position = EntityPosition(e);
                    const Float3 velocity = (c.hasPreviousPosition && deltaTime > 0.0f)
                        ? Float3{ (position.x - c.previousPosition.x) / deltaTime,
                                  (position.y - c.previousPosition.y) / deltaTime,
                                  (position.z - c.previousPosition.z) / deltaTime }
                        : Float3{ 0.0f, 0.0f, 0.0f };
                    m_engine->SetVoicePosition(c.voice, position, velocity);
                    c.previousPosition = position;
                    c.hasPreviousPosition = true;
                });
            }

            UpdateListenerPose(deltaTime);
        }

        // The scene's listener pose this frame (component-driven). The SUBSYSTEM pushes
        // the winning scene's pose to the engine (camera fallback handled there).
        [[nodiscard]] bool ListenerValid() const noexcept { return m_listenerValid; }
        [[nodiscard]] Float3 ListenerPosition() const noexcept { return m_listenerPosition; }
        [[nodiscard]] Float3 ListenerForward() const noexcept { return m_listenerForward; }
        [[nodiscard]] Float3 ListenerUp() const noexcept { return m_listenerUp; }
        [[nodiscard]] Float3 ListenerVelocity() const noexcept { return m_listenerVelocity; }

    private:
        [[nodiscard]] AudioSourceComponent* Component(dscene::EntityHandle entity)
        {
            auto* sources = m_scene != nullptr
                ? m_scene->GetSystem<AudioSourceComponentManager>() : nullptr;
            return sources != nullptr ? sources->Get(entity) : nullptr;
        }

        [[nodiscard]] Float3 EntityPosition(dscene::EntityHandle entity) const
        {
            const Float4x4 world = m_scene->GetWorldMatrix(entity);
            return TransformPoint(Float3{ 0.0f, 0.0f, 0.0f }, world);
        }

        VoiceHandle PlayComponent(AudioSourceComponent& c, dscene::EntityHandle e)
        {
            if (m_engine == nullptr) { return {}; }
            AudioClip* clip = c.clip.Get();
            if (clip == nullptr)
            {
                DRACONIC_LOG_WARNING(u8"Audio", u8"'{}': audio source has no clip",
                                     m_scene->GetEntityName(e));
                return {};
            }
            if (m_engine->IsValidHandle(c.voice)) { m_engine->Stop(c.voice); }

            AudioPlayParams params;
            params.bus = c.bus;
            params.volume = c.volume;
            params.pitch = c.pitch;
            params.loop = c.loop;
            params.priority = c.priority;
            params.sceneGroup = m_sceneGroup;
            params.spatial = c.spatial;
            if (c.spatial)
            {
                params.position = EntityPosition(e);
                params.minDistance = c.minDistance;
                params.maxDistance = c.maxDistance;
                params.attenuationModel = c.attenuationModel;
                params.rolloff = c.rolloff;
                params.dopplerFactor = c.dopplerFactor;
                params.coneInnerAngleDegrees = c.coneInnerAngleDegrees;
                params.coneOuterAngleDegrees = c.coneOuterAngleDegrees;
                params.coneOuterGain = c.coneOuterGain;
            }
            c.voice = m_engine->Play(RefPtr<AudioClip>(clip), params);
            c.previousPosition = params.position;
            c.hasPreviousPosition = c.spatial;
            return c.voice;
        }

        void UpdateListenerPose(f32 deltaTime)
        {
            m_listenerValid = false;
            auto* listeners = m_scene->GetSystem<AudioListenerComponentManager>();
            if (listeners == nullptr) { return; }
            listeners->ForEach([&](AudioListenerComponent& c, dscene::EntityHandle e) {
                if (m_listenerValid || !c.isActive) { return; }
                const Float4x4 world = m_scene->GetWorldMatrix(e);
                const Float3 position = TransformPoint(Float3{ 0.0f, 0.0f, 0.0f }, world);
                // Forward is -Z (row 2 negated), up is +Y (row 1) - row-vector convention.
                m_listenerForward = Normalized(
                    Float3{ -world.m[2][0], -world.m[2][1], -world.m[2][2] });
                m_listenerUp = Normalized(Float3{ world.m[1][0], world.m[1][1], world.m[1][2] });
                m_listenerVelocity = (c.hasPreviousPosition && deltaTime > 0.0f)
                    ? Float3{ (position.x - c.previousPosition.x) / deltaTime,
                              (position.y - c.previousPosition.y) / deltaTime,
                              (position.z - c.previousPosition.z) / deltaTime }
                    : Float3{ 0.0f, 0.0f, 0.0f };
                m_listenerPosition = position;
                c.previousPosition = position;
                c.hasPreviousPosition = true;
                m_listenerValid = true;
            });
        }

        dscene::Scene* m_scene = nullptr;
        AudioEngine* m_engine = nullptr;
        u64 m_sceneGroup = 0;
        bool m_started = false;
        bool m_wasSimulating = true;
        bool m_listenerValid = false;
        Float3 m_listenerPosition{ 0.0f, 0.0f, 0.0f };
        Float3 m_listenerForward{ 0.0f, 0.0f, -1.0f };
        Float3 m_listenerUp{ 0.0f, 1.0f, 0.0f };
        Float3 m_listenerVelocity{ 0.0f, 0.0f, 0.0f };
    };

    // The runtime subsystem: owns the ONE AudioEngine, injects the managers + system
    // into every scene (ISceneAware), pushes the winning listener, and exposes the
    // engine-global one-shot API (docs/design/audio.md §6).
    class AudioSubsystem final : public draconic::runtime::Subsystem,
                                 public dscene::ISceneAware
    {
    public:
        explicit AudioSubsystem(const AudioEngineSettings& engineSettings = {})
            : m_engineSettings(engineSettings) {}

        [[nodiscard]] AudioEngine* Engine() const noexcept { return m_engine.Get(); }

        // AFTER the scene subsystem (-500): voice/listener sync ran during the scene's
        // PostTransform phase; here the engine reaps + pumps and the listener lands.
        [[nodiscard]] i32 UpdateOrder() const noexcept override { return -100; }

        void OnSceneCreated(dscene::Scene& scene) override
        {
            scene.AddSystem<AudioSourceComponentManager>();
            scene.AddSystem<AudioListenerComponentManager>();
            AudioSceneSystem* system = scene.AddSystem<AudioSceneSystem>();
            system->SetEngine(m_engine.Get());
            m_systems.PushBack(SceneEntry{ &scene, system });
        }
        void OnSceneDestroyed(dscene::Scene& scene) override
        {
            for (usize i = 0; i < m_systems.Size(); ++i)
            {
                if (m_systems[i].scene == &scene) { m_systems.RemoveAt(i); return; }
            }
        }

        // Defined in SubsystemImpl.cpp: listener push (active camera fallback = render
        // dep) + the engine tick.
        void Update(f32 deltaTime) override;

        // ---- engine-global one-shots (docs/design/audio.md §6) ----

        [[nodiscard]] VoiceHandle PlayOneShot(const RefPtr<AudioClip>& clip,
                                              AudioBus bus = AudioBus::Effects,
                                              f32 volume = 1.0f, f32 pitch = 1.0f)
        {
            if (m_engine.Get() == nullptr) { return {}; }
            AudioPlayParams params;
            params.bus = bus;
            params.volume = volume;
            params.pitch = pitch;
            return m_engine->Play(clip, params);
        }

        [[nodiscard]] VoiceHandle PlayOneShot3D(const RefPtr<AudioClip>& clip, Float3 position,
                                                const AudioPlayParams& baseParams = {})
        {
            if (m_engine.Get() == nullptr) { return {}; }
            AudioPlayParams params = baseParams;
            params.spatial = true;
            params.position = position;
            return m_engine->Play(clip, params);
        }

        void Stop(VoiceHandle handle)
        {
            if (m_engine.Get() != nullptr) { m_engine->Stop(handle); }
        }
        [[nodiscard]] bool IsPlaying(VoiceHandle handle) const
        {
            return m_engine.Get() != nullptr && m_engine->IsPlaying(handle);
        }

        void SetBusVolume(AudioBus bus, f32 volume)
        {
            if (m_engine.Get() != nullptr) { m_engine->SetBusVolume(bus, volume); }
        }
        [[nodiscard]] f32 BusVolume(AudioBus bus) const
        {
            return m_engine.Get() != nullptr ? m_engine->BusVolume(bus) : 0.0f;
        }

    protected:
        void OnInit() override
        {
            m_engine = MakeUnique<AudioEngine>(DefaultAllocator(), m_engineSettings);
            for (const SceneEntry& entry : m_systems)
            {
                entry.system->SetEngine(m_engine.Get());   // scenes created pre-init
            }
            RegisterAudioComponentReflection();
        }
        void OnReady() override
        {
            if (draconic::runtime::Context* context = GetContext())
            {
                if (auto* scenes = context->GetSubsystem<dscene::SceneSubsystem>())
                {
                    scenes->RegisterSceneAware(this);
                }
            }
        }
        void OnShutdown() override
        {
            if (draconic::runtime::Context* context = GetContext())
            {
                if (auto* scenes = context->GetSubsystem<dscene::SceneSubsystem>())
                {
                    scenes->UnregisterSceneAware(this);
                }
            }
            m_engine = nullptr;
        }

        struct SceneEntry
        {
            dscene::Scene* scene = nullptr;
            AudioSceneSystem* system = nullptr;
        };
        [[nodiscard]] Span<const SceneEntry> Systems() const noexcept
        {
            return Span<const SceneEntry>{ m_systems.Data(), m_systems.Size() };
        }

    private:
        AudioEngineSettings m_engineSettings;
        UniquePtr<AudioEngine> m_engine;
        Array<SceneEntry> m_systems;
    };
}
