// draconic.particles:effect - the particle object model, ported from Sedulous.Particles
// (ParticleEmitter.bf, ParticleSystem.bf, ParticleEffect.bf, ParticleEffectInstance.bf).
//
//   ParticleEffect        - asset definition: a list of systems + sub-emitter links.
//   ParticleSystem        - the workhorse: emitter + initializers + behaviors + streams +
//                           simulator; runs the per-frame Update loop, spawn, LOD, events.
//   ParticleEmitter       - spawn timing only (continuous rate / bursts); returns a count.
//   ParticleEffectInstance- a runtime instance over a shared effect; drives Update + routes
//                           sub-emitter birth/death events between systems.
//
// Trails, mesh/texture/material refs, and serialization are later layers; this is the CPU
// simulation core driven by an effect built in code (or, later, by the cooked resource).

module;
#include "Core/Prelude.h"
#include <utility>   // std::move / std::forward (module-local; core Move/Forward also exist)

export module draconic.particles:effect;

import draconic.core;
import :types;
import :streams;
import :modules;

using namespace draconic::core;

export namespace draconic::particles
{
    // ---- ParticleEmitter (spawn timing only) -------------------------------------------------

    enum class EmissionMode : u8 { Continuous, Burst, ContinuousAndBurst };

    class ParticleEmitter
    {
    public:
        EmissionMode mode = EmissionMode::Continuous;
        f32 spawnRate = 10.0f;      // particles/second (continuous)
        i32 burstCount = 0;         // particles per burst
        f32 burstInterval = 0.0f;   // seconds between bursts (<=0 = single burst on first frame)
        i32 burstCycles = 0;        // 0 = infinite bursts
        bool isEmitting = true;

        // Returns how many particles to spawn this frame (does not spawn). Accumulator model for
        // continuous; timer/cycles for bursts.
        [[nodiscard]] i32 CalculateSpawnCount(f32 deltaTime) noexcept
        {
            if (!isEmitting) { return 0; }
            i32 count = 0;
            if (mode == EmissionMode::Continuous || mode == EmissionMode::ContinuousAndBurst)
            {
                m_spawnAccumulator += spawnRate * deltaTime;
                const i32 whole = static_cast<i32>(m_spawnAccumulator);
                count += whole;
                m_spawnAccumulator -= static_cast<f32>(whole);
            }
            if (mode == EmissionMode::Burst || mode == EmissionMode::ContinuousAndBurst)
            {
                if (burstInterval <= 0.0f)
                {
                    if (!m_singleBurstDone) { count += burstCount; m_singleBurstDone = true; }
                }
                else
                {
                    m_burstTimer += deltaTime;
                    while (m_burstTimer >= burstInterval && (burstCycles == 0 || m_burstCyclesCompleted < burstCycles))
                    {
                        count += burstCount;
                        m_burstTimer -= burstInterval;
                        ++m_burstCyclesCompleted;
                    }
                }
            }
            return count;
        }

        void Reset() noexcept
        {
            m_spawnAccumulator = 0.0f; m_burstTimer = 0.0f; m_burstCyclesCompleted = 0; m_singleBurstDone = false;
        }

    private:
        f32 m_spawnAccumulator = 0.0f;
        f32 m_burstTimer = 0.0f;
        i32 m_burstCyclesCompleted = 0;
        bool m_singleBurstDone = false;
    };

    // ---- ParticleSystem ----------------------------------------------------------------------

    inline constexpr i32 kMaxEventsPerFrame = 64;

    class ParticleSystem
    {
    public:
        // Config
        String name;
        SimulationMode desiredMode = SimulationMode::CPU;
        ParticleSpace simulationSpace = ParticleSpace::World;
        ParticleBlendMode blendMode = ParticleBlendMode::Alpha;
        ParticleRenderMode renderMode = ParticleRenderMode::Billboard;
        bool sortParticles = false;
        // LOD (0 disables)
        f32 lodStartDistance = 0.0f;
        f32 lodCullDistance = 0.0f;
        f32 lodMinRate = 0.1f;
        // Runtime transform state
        Vector3 position{ 0.0f, 0.0f, 0.0f };
        Vector3 prevPosition{ 0.0f, 0.0f, 0.0f };

        ParticleEmitter emitter;

        explicit ParticleSystem(i32 maxParticles, u64 seed = 0x9E3779B97F4A7C15ull)
            : m_maxParticles(maxParticles), m_streams(maxParticles),
              m_simulator(MakeUnique<CPUSimulator>(DefaultAllocator())), m_random(seed)
        {
        }

        ParticleSystem(const ParticleSystem&) = delete;
        ParticleSystem& operator=(const ParticleSystem&) = delete;

        [[nodiscard]] i32 MaxParticles() const noexcept { return m_maxParticles; }
        [[nodiscard]] i32 AliveCount() const noexcept { return m_streams.aliveCount; }
        [[nodiscard]] f32 TotalTime() const noexcept { return m_totalTime; }
        [[nodiscard]] f32 LODRateMultiplier() const noexcept { return m_lodRateMultiplier; }
        [[nodiscard]] bool IsLODCulled() const noexcept { return m_lodRateMultiplier <= 0.0f && m_streams.aliveCount == 0; }
        [[nodiscard]] ParticleStreamContainer& Streams() noexcept { return m_streams; }
        [[nodiscard]] const ParticleStreamContainer& Streams() const noexcept { return m_streams; }
        [[nodiscard]] SimulationMode ResolvedMode() const noexcept { return m_resolvedMode; }

        // Build helpers: construct+configure a module in place, declare its streams, own it.
        template <typename T, typename... Args>
        T& AddInitializer(Args&&... args)
        {
            UniquePtr<T> p = MakeUnique<T>(DefaultAllocator(), std::forward<Args>(args)...);
            T& ref = *p;
            ref.DeclareStreams(m_streams);
            m_initializers.PushBack(UniquePtr<ParticleInitializer>(std::move(p)));
            return ref;
        }
        template <typename T, typename... Args>
        T& AddBehavior(Args&&... args)
        {
            UniquePtr<T> p = MakeUnique<T>(DefaultAllocator(), std::forward<Args>(args)...);
            T& ref = *p;
            ref.DeclareStreams(m_streams);
            m_behaviors.PushBack(UniquePtr<ParticleBehavior>(std::move(p)));
            return ref;
        }

        // Resolve CPU/GPU/Auto against behavior support. GPU is stubbed (Phase 6) - Auto/GPU still
        // fall back to CPU here, but the decision is recorded for when the GPU simulator lands.
        void ResolveSimulationMode() noexcept
        {
            switch (desiredMode)
            {
                case SimulationMode::CPU: m_resolvedMode = SimulationMode::CPU; break;
                case SimulationMode::GPU:
                {
                    m_resolvedMode = SimulationMode::GPU;
                    for (usize i = 0; i < m_behaviors.Size(); ++i)
                    {
                        if (m_behaviors[i]->Support() == BehaviorSupport::CPUOnly) { m_resolvedMode = SimulationMode::CPU; break; }
                    }
                    break;
                }
                case SimulationMode::Auto:
                {
                    bool allSupportGpu = true;
                    for (usize i = 0; i < m_behaviors.Size(); ++i)
                    {
                        if (m_behaviors[i]->Support() == BehaviorSupport::CPUOnly) { allSupportGpu = false; break; }
                    }
                    m_resolvedMode = (allSupportGpu && m_maxParticles > 1024) ? SimulationMode::GPU : SimulationMode::CPU;
                    break;
                }
            }
            // Until the GPU simulator exists, everything simulates on CPU.
        }

        // The per-frame step - exact Sedulous order.
        void Update(f32 deltaTime, Vector3 cameraPos = Vector3::Zero)
        {
            m_totalTime += deltaTime;                                   // 1
            m_deathCount = 0; m_birthCount = 0;                         // 2
            m_lodRateMultiplier = CalculateLODMultiplier(cameraPos);    // 3
            i32 spawnCount = emitter.CalculateSpawnCount(deltaTime);    // 4
            spawnCount = (m_lodRateMultiplier <= 0.0f) ? 0 : static_cast<i32>(static_cast<f32>(spawnCount) * m_lodRateMultiplier);
            SpawnParticles(spawnCount);
            ParticleUpdateContext ctx{ m_totalTime, deltaTime, position, &m_random };   // 5
            m_simulator->Simulate(m_streams, m_behaviors, ctx);         // 6
            IntegrateVelocityAndAge(deltaTime);                        // 7 (hardcoded finalize)
            CollectDeathEvents();                                      // 9 (before compaction)
            m_streams.CompactDead();                                   // 10
            prevPosition = position;                                   // 11
        }

        // Spawn `count` new particles through the emitter timing (used by Update).
        void SpawnParticles(i32 count) { SpawnInternal(count, false, Vector3::Zero); }
        // Spawn immediately, bypassing emitter timing (sub-emitter birth without inheritance).
        void SpawnImmediate(i32 count) { SpawnInternal(count, false, Vector3::Zero); }
        // Spawn at a specific position (sub-emitter with inherited position). inheritedVelocity/
        // Color are accepted for parity but not yet written to streams (matches Sedulous).
        void SpawnAt(i32 count, Vector3 spawnPos, Vector3 inheritedVelocity = Vector3::Zero, Vector4 inheritedColor = Vector4{ 1.0f, 1.0f, 1.0f, 1.0f })
        {
            (void)inheritedVelocity; (void)inheritedColor;
            SpawnInternal(count, true, spawnPos);
        }

        [[nodiscard]] Span<const ParticleEvent> DeathEvents() const noexcept { return Span<const ParticleEvent>{ m_deathEvents, static_cast<usize>(m_deathCount) }; }
        [[nodiscard]] Span<const ParticleEvent> BirthEvents() const noexcept { return Span<const ParticleEvent>{ m_birthEvents, static_cast<usize>(m_birthCount) }; }

        void Reset() noexcept
        {
            m_streams.aliveCount = 0;
            emitter.Reset();
            m_totalTime = 0.0f;
        }

    private:
        void SpawnInternal(i32 count, bool overridePosition, Vector3 spawnPos)
        {
            if (count <= 0) { return; }
            const Vector3 emitterVel = (position - prevPosition) / Max(m_totalTime, 0.001f);   // Sedulous divides by TotalTime (kept faithful)
            for (usize k = 0; k < m_initializers.Size(); ++k) { m_initializers[k]->SetEmitterState(position, emitterVel); }
            for (i32 n = 0; n < count; ++n)
            {
                if (m_streams.aliveCount >= m_maxParticles) { break; }
                const i32 index = m_streams.aliveCount++;
                for (usize k = 0; k < m_initializers.Size(); ++k) { m_initializers[k]->Initialize(m_streams, index, m_random); }
                if (overridePosition) { (*m_streams.Positions())[index] = spawnPos; }
                RecordBirthEvent(index);
            }
        }

        void IntegrateVelocityAndAge(f32 deltaTime) noexcept
        {
            CPUStream<Vector3>* pos = m_streams.Positions();
            CPUStream<Vector3>* vel = m_streams.Velocities();
            CPUStream<f32>* ages = m_streams.Ages();
            const bool applyVel = (pos != nullptr && vel != nullptr);
            for (i32 i = 0; i < m_streams.aliveCount; ++i)
            {
                if (applyVel) { (*pos)[i] += (*vel)[i] * deltaTime; }
                if (ages != nullptr) { (*ages)[i] += deltaTime; }
            }
        }

        void RecordBirthEvent(i32 index) noexcept
        {
            if (m_birthCount >= kMaxEventsPerFrame) { return; }
            ParticleEvent e;
            e.position = (*m_streams.Positions())[index];
            if (CPUStream<Vector3>* vel = m_streams.Velocities()) { e.velocity = (*vel)[index]; }
            if (CPUStream<Vector4>* col = m_streams.Colors()) { e.color = (*col)[index]; }
            m_birthEvents[m_birthCount++] = e;
        }

        void CollectDeathEvents() noexcept
        {
            CPUStream<f32>* ages = m_streams.Ages();
            CPUStream<f32>* lifetimes = m_streams.Lifetimes();
            if (ages == nullptr || lifetimes == nullptr) { return; }
            CPUStream<Vector3>* pos = m_streams.Positions();
            CPUStream<Vector3>* vel = m_streams.Velocities();
            CPUStream<Vector4>* col = m_streams.Colors();
            for (i32 i = 0; i < m_streams.aliveCount; ++i)
            {
                if ((*ages)[i] < (*lifetimes)[i]) { continue; }
                if (m_deathCount >= kMaxEventsPerFrame) { break; }
                ParticleEvent e;
                if (pos != nullptr) { e.position = (*pos)[i]; }
                if (vel != nullptr) { e.velocity = (*vel)[i]; }
                if (col != nullptr) { e.color = (*col)[i]; }
                m_deathEvents[m_deathCount++] = e;
            }
        }

        [[nodiscard]] f32 CalculateLODMultiplier(Vector3 cameraPos) const noexcept
        {
            if (lodStartDistance <= 0.0f && lodCullDistance <= 0.0f) { return 1.0f; }   // disabled
            const f32 dist = Length(position - cameraPos);
            if (dist <= lodStartDistance) { return 1.0f; }
            if (lodCullDistance > 0.0f && dist >= lodCullDistance) { return 0.0f; }
            if (lodCullDistance <= lodStartDistance) { return 1.0f; }
            const f32 t = (dist - lodStartDistance) / (lodCullDistance - lodStartDistance);
            return Max(1.0f - t * (1.0f - lodMinRate), lodMinRate);
        }

        i32 m_maxParticles;
        ParticleStreamContainer m_streams;
        Array<UniquePtr<ParticleInitializer>> m_initializers;
        Array<UniquePtr<ParticleBehavior>> m_behaviors;
        UniquePtr<ParticleSimulator> m_simulator;
        Random m_random;
        f32 m_totalTime = 0.0f;
        f32 m_lodRateMultiplier = 1.0f;
        SimulationMode m_resolvedMode = SimulationMode::CPU;
        ParticleEvent m_deathEvents[kMaxEventsPerFrame]{};
        ParticleEvent m_birthEvents[kMaxEventsPerFrame]{};
        i32 m_deathCount = 0;
        i32 m_birthCount = 0;
    };

    // ---- ParticleEffect (asset definition) ---------------------------------------------------

    class ParticleEffect
    {
    public:
        String name;

        explicit ParticleEffect(StringView effectName = StringView(u8"Effect")) : name(effectName) {}

        // Create + own a new system, returning a reference to configure it.
        ParticleSystem& AddSystem(i32 maxParticles, u64 seed = 0x9E3779B97F4A7C15ull)
        {
            UniquePtr<ParticleSystem> sys = MakeUnique<ParticleSystem>(DefaultAllocator(), maxParticles, seed);
            ParticleSystem& ref = *sys;
            m_systems.PushBack(std::move(sys));
            return ref;
        }

        void AddSubEmitterLink(SubEmitterLink link) { m_links.PushBack(link); }

        [[nodiscard]] i32 SystemCount() const noexcept { return static_cast<i32>(m_systems.Size()); }
        [[nodiscard]] ParticleSystem* GetSystem(i32 index) noexcept
        {
            if (index < 0 || static_cast<usize>(index) >= m_systems.Size()) { return nullptr; }
            return m_systems[static_cast<usize>(index)].Get();
        }
        [[nodiscard]] Span<const SubEmitterLink> SubEmitterLinks() const noexcept { return Span<const SubEmitterLink>{ m_links.Data(), m_links.Size() }; }

    private:
        Array<UniquePtr<ParticleSystem>> m_systems;
        Array<SubEmitterLink> m_links;
    };

    // ---- ParticleEffectInstance (runtime) ----------------------------------------------------

    class ParticleEffectInstance
    {
    public:
        Vector3 position{ 0.0f, 0.0f, 0.0f };
        bool isActive = true;

        explicit ParticleEffectInstance(ParticleEffect& effect) noexcept : m_effect(&effect) {}

        [[nodiscard]] ParticleEffect& Effect() const noexcept { return *m_effect; }

        void Update(f32 deltaTime, Vector3 cameraPos = Vector3::Zero)
        {
            if (!isActive || m_effect == nullptr) { return; }
            const i32 count = m_effect->SystemCount();
            for (i32 i = 0; i < count; ++i)
            {
                ParticleSystem* sys = m_effect->GetSystem(i);
                sys->position = position;
                sys->Update(deltaTime, cameraPos);
            }
            RouteSubEmitterEvents();
        }

        // True once every system has stopped emitting and drained its live particles.
        [[nodiscard]] bool IsFinished() const noexcept
        {
            if (m_effect == nullptr) { return true; }
            const i32 count = m_effect->SystemCount();
            for (i32 i = 0; i < count; ++i)
            {
                ParticleSystem* sys = m_effect->GetSystem(i);
                if (sys->AliveCount() > 0 || sys->emitter.isEmitting) { return false; }
            }
            return true;
        }

        void Stop() noexcept
        {
            if (m_effect == nullptr) { return; }
            for (i32 i = 0; i < m_effect->SystemCount(); ++i) { m_effect->GetSystem(i)->emitter.isEmitting = false; }
        }

        void Reset() noexcept
        {
            if (m_effect == nullptr) { return; }
            for (i32 i = 0; i < m_effect->SystemCount(); ++i) { m_effect->GetSystem(i)->Reset(); }
        }

    private:
        void RouteSubEmitterEvents()
        {
            const Span<const SubEmitterLink> links = m_effect->SubEmitterLinks();
            const i32 systemCount = m_effect->SystemCount();
            for (usize li = 0; li < links.Size(); ++li)
            {
                const SubEmitterLink& link = links[li];
                if (link.childSystemIndex < 0 || link.childSystemIndex >= systemCount) { continue; }
                ParticleSystem* child = m_effect->GetSystem(link.childSystemIndex);
                for (i32 s = 0; s < systemCount; ++s)
                {
                    if (s == link.childSystemIndex) { continue; }   // don't route a system into itself
                    ParticleSystem* parent = m_effect->GetSystem(s);
                    const Span<const ParticleEvent> events = (link.trigger == ParticleEventType::OnDeath) ? parent->DeathEvents() : parent->BirthEvents();
                    for (usize e = 0; e < events.Size(); ++e)
                    {
                        const ParticleEvent& evt = events[e];
                        if (link.probability < 1.0f)
                        {
                            // Deterministic spatial-hash gate (matches Sedulous - stable per position).
                            const u32 hash = static_cast<u32>(evt.position.x * 73856093.0f) ^ static_cast<u32>(evt.position.y * 19349663.0f);
                            if (static_cast<f32>(hash % 1000u) / 1000.0f > link.probability) { continue; }
                        }
                        if (link.inheritPosition)
                        {
                            const Vector3 vel = link.inheritVelocity ? evt.velocity * link.velocityInheritFactor : Vector3::Zero;
                            const Vector4 col = link.inheritColor ? evt.color : Vector4{ 1.0f, 1.0f, 1.0f, 1.0f };
                            child->SpawnAt(link.spawnCount, evt.position, vel, col);
                        }
                        else
                        {
                            child->SpawnImmediate(link.spawnCount);
                        }
                    }
                }
            }
        }

        ParticleEffect* m_effect = nullptr;
    };
}
