// draconic.particles:modules - the initializer/behavior module taxonomy + the concrete
// modules + the CPU simulator + the runtime type-id registry. Ported from Sedulous.Particles
// (ParticleInitializer.bf, ParticleBehavior.bf, ParticleSimulator.bf, CPUSimulator.bf,
// Initializers/*, Behaviors/*, ParticleTypeRegistry.bf).
//
// An INITIALIZER runs once per spawned particle; a BEHAVIOR runs every frame over all live
// particles. Each declares the streams it needs (lazy allocation). Velocity integration + aging
// are a hardcoded final step on ParticleSystem (not a module). Render "type" is an enum, not a
// module. The GPU simulator is deferred (Phase 6) behind the same interfaces + BehaviorSupport.

module;
#include "Core/Prelude.h"

export module draconic.particles:modules;

import draconic.core;
import :types;
import :streams;

using namespace draconic::core;

export namespace draconic::particles
{
    // ---- Base classes ------------------------------------------------------------------------

    class ParticleInitializer
    {
    public:
        virtual ~ParticleInitializer() = default;
        [[nodiscard]] virtual BehaviorSupport Support() const noexcept = 0;
        virtual void DeclareStreams(ParticleStreamContainer& streams) = 0;
        virtual void Initialize(ParticleStreamContainer& streams, i32 index, Random& rng) = 0;
        // Hook: the system pushes its transform state before a spawn burst so emitter-aware
        // initializers (Position/Velocity) can offset/inherit. Default no-op (avoids an RTTI cast).
        virtual void SetEmitterState(Vector3 position, Vector3 velocity) noexcept { (void)position; (void)velocity; }
    };

    class ParticleBehavior
    {
    public:
        virtual ~ParticleBehavior() = default;
        [[nodiscard]] virtual BehaviorSupport Support() const noexcept = 0;
        virtual void DeclareStreams(ParticleStreamContainer& streams) = 0;
        virtual void Update(ParticleStreamContainer& streams, ParticleUpdateContext& ctx) = 0;
    };

    class ParticleSimulator
    {
    public:
        virtual ~ParticleSimulator() = default;
        virtual void Simulate(ParticleStreamContainer& streams, const Array<UniquePtr<ParticleBehavior>>& behaviors, ParticleUpdateContext& ctx) = 0;
        virtual i32 CompactDead(ParticleStreamContainer& streams) = 0;
    };

    // ---- Initializers ------------------------------------------------------------------------

    class PositionInitializer final : public ParticleInitializer
    {
    public:
        EmissionShape shape = EmissionShape::Point();
        Vector3 emitterPosition{ 0.0f, 0.0f, 0.0f };   // set by the system each spawn (hidden)
        bool localSpace = false;

        [[nodiscard]] BehaviorSupport Support() const noexcept override { return BehaviorSupport::Both; }
        void DeclareStreams(ParticleStreamContainer&) override {}   // Position is a core stream
        void SetEmitterState(Vector3 position, Vector3) noexcept override { emitterPosition = position; }
        void Initialize(ParticleStreamContainer& streams, i32 index, Random& rng) override
        {
            Vector3 pos, dir;
            shape.Sample(rng, pos, dir);
            (*streams.Positions())[index] = localSpace ? pos : emitterPosition + pos;
        }
    };

    class VelocityInitializer final : public ParticleInitializer
    {
    public:
        Vector3 baseVelocity{ 0.0f, 1.0f, 0.0f };
        Vector3 randomness{ 0.0f, 0.0f, 0.0f };
        f32 shapeDirectionSpeed = 0.0f;
        f32 velocityInheritance = 0.0f;
        EmissionShape shape = EmissionShape::Point();
        Vector3 emitterVelocity{ 0.0f, 0.0f, 0.0f };   // set by the system each spawn (hidden)

        [[nodiscard]] BehaviorSupport Support() const noexcept override { return BehaviorSupport::Both; }
        void DeclareStreams(ParticleStreamContainer& streams) override
        {
            streams.EnsureStream(ParticleStreamId::Velocity, StreamElementType::Float3);
            streams.EnsureStream(ParticleStreamId::StartVelocity, StreamElementType::Float3);
        }
        void SetEmitterState(Vector3, Vector3 velocity) noexcept override { emitterVelocity = velocity; }
        void Initialize(ParticleStreamContainer& streams, i32 index, Random& rng) override
        {
            Vector3 pos, dir;
            shape.Sample(rng, pos, dir);
            const Vector3 rnd{ rng.NextFloat(-randomness.x, randomness.x),
                               rng.NextFloat(-randomness.y, randomness.y),
                               rng.NextFloat(-randomness.z, randomness.z) };
            const Vector3 v = baseVelocity + rnd + dir * shapeDirectionSpeed + emitterVelocity * velocityInheritance;
            (*streams.Velocities())[index] = v;
            (*streams.StartVelocities())[index] = v;
        }
    };

    class LifetimeInitializer final : public ParticleInitializer
    {
    public:
        RangeFloat lifetime{ 1.0f, 1.0f };
        [[nodiscard]] BehaviorSupport Support() const noexcept override { return BehaviorSupport::Both; }
        void DeclareStreams(ParticleStreamContainer&) override {}   // Age/Lifetime are core
        void Initialize(ParticleStreamContainer& streams, i32 index, Random& rng) override
        {
            (*streams.Lifetimes())[index] = Max(lifetime.Evaluate(rng.NextFloat()), 0.01f);
            (*streams.Ages())[index] = 0.0f;
        }
    };

    class ColorInitializer final : public ParticleInitializer
    {
    public:
        RangeColor color = RangeColor::Constant(Vector4{ 1.0f, 1.0f, 1.0f, 1.0f });
        [[nodiscard]] BehaviorSupport Support() const noexcept override { return BehaviorSupport::Both; }
        void DeclareStreams(ParticleStreamContainer& streams) override { streams.EnsureStream(ParticleStreamId::Color, StreamElementType::Float4); }
        void Initialize(ParticleStreamContainer& streams, i32 index, Random& rng) override
        {
            (*streams.Colors())[index] = color.Evaluate(rng.NextFloat());
        }
    };

    class SizeInitializer final : public ParticleInitializer
    {
    public:
        RangeVector2 size = RangeVector2::Constant(Vector2{ 0.1f, 0.1f });
        [[nodiscard]] BehaviorSupport Support() const noexcept override { return BehaviorSupport::Both; }
        void DeclareStreams(ParticleStreamContainer& streams) override { streams.EnsureStream(ParticleStreamId::Size, StreamElementType::Float2); }
        void Initialize(ParticleStreamContainer& streams, i32 index, Random& rng) override
        {
            (*streams.Sizes())[index] = size.Evaluate(rng.NextFloat());
        }
    };

    class RotationInitializer final : public ParticleInitializer
    {
    public:
        RangeFloat rotation{ 0.0f, 6.2831853f };
        RangeFloat rotationSpeed{ -2.0f, 2.0f };
        [[nodiscard]] BehaviorSupport Support() const noexcept override { return BehaviorSupport::Both; }
        void DeclareStreams(ParticleStreamContainer& streams) override
        {
            streams.EnsureStream(ParticleStreamId::Rotation, StreamElementType::Float);
            streams.EnsureStream(ParticleStreamId::RotationSpeed, StreamElementType::Float);
        }
        void Initialize(ParticleStreamContainer& streams, i32 index, Random& rng) override
        {
            (*streams.Rotations())[index] = rotation.Evaluate(rng.NextFloat());
            (*streams.RotationSpeeds())[index] = rotationSpeed.Evaluate(rng.NextFloat());
        }
    };

    class MeshOrientationInitializer final : public ParticleInitializer
    {
    public:
        bool randomAxis = true;
        Vector3 fixedAxis{ 0.0f, 1.0f, 0.0f };
        [[nodiscard]] BehaviorSupport Support() const noexcept override { return BehaviorSupport::Both; }
        void DeclareStreams(ParticleStreamContainer& streams) override { streams.EnsureStream(ParticleStreamId::Axis, StreamElementType::Float3); }
        void Initialize(ParticleStreamContainer& streams, i32 index, Random& rng) override
        {
            Vector3 axis;
            if (randomAxis)
            {
                const f32 z = rng.NextFloat(-1.0f, 1.0f);
                const f32 phi = rng.NextFloat(0.0f, 6.2831853f);
                const f32 r = Sqrt(Max(1.0f - z * z, 0.0f));
                axis = Vector3{ r * Cos(phi), r * Sin(phi), z };
            }
            else
            {
                axis = (LengthSquared(fixedAxis) > 1e-6f) ? Normalized(fixedAxis) : Vector3::UnitY;
            }
            (*streams.Axes())[index] = axis;
        }
    };

    // ---- Behaviors ---------------------------------------------------------------------------

    class GravityBehavior final : public ParticleBehavior
    {
    public:
        f32 multiplier = 1.0f;
        Vector3 direction{ 0.0f, -1.0f, 0.0f };
        [[nodiscard]] BehaviorSupport Support() const noexcept override { return BehaviorSupport::Both; }
        void DeclareStreams(ParticleStreamContainer& streams) override { streams.EnsureStream(ParticleStreamId::Velocity, StreamElementType::Float3); }
        void Update(ParticleStreamContainer& streams, ParticleUpdateContext& ctx) override
        {
            CPUStream<Vector3>* vel = streams.Velocities();
            if (vel == nullptr) { return; }
            const Vector3 dv = direction * (9.81f * multiplier * ctx.deltaTime);
            for (i32 i = 0; i < streams.aliveCount; ++i) { (*vel)[i] += dv; }
        }
    };

    class DragBehavior final : public ParticleBehavior
    {
    public:
        f32 drag = 1.0f;
        [[nodiscard]] BehaviorSupport Support() const noexcept override { return BehaviorSupport::Both; }
        void DeclareStreams(ParticleStreamContainer& streams) override { streams.EnsureStream(ParticleStreamId::Velocity, StreamElementType::Float3); }
        void Update(ParticleStreamContainer& streams, ParticleUpdateContext& ctx) override
        {
            CPUStream<Vector3>* vel = streams.Velocities();
            if (vel == nullptr) { return; }
            const f32 factor = Max(1.0f - drag * ctx.deltaTime, 0.0f);
            for (i32 i = 0; i < streams.aliveCount; ++i) { (*vel)[i] *= factor; }
        }
    };

    class WindBehavior final : public ParticleBehavior
    {
    public:
        Vector3 force{ 1.0f, 0.0f, 0.0f };
        f32 turbulence = 0.0f;
        [[nodiscard]] BehaviorSupport Support() const noexcept override { return BehaviorSupport::Both; }
        void DeclareStreams(ParticleStreamContainer& streams) override { streams.EnsureStream(ParticleStreamId::Velocity, StreamElementType::Float3); }
        void Update(ParticleStreamContainer& streams, ParticleUpdateContext& ctx) override
        {
            CPUStream<Vector3>* vel = streams.Velocities();
            if (vel == nullptr) { return; }
            Random& rng = *ctx.rng;
            for (i32 i = 0; i < streams.aliveCount; ++i)
            {
                const Vector3 t{ rng.NextFloat(-turbulence, turbulence), rng.NextFloat(-turbulence, turbulence), rng.NextFloat(-turbulence, turbulence) };
                (*vel)[i] += (force + t) * ctx.deltaTime;
            }
        }
    };

    class TurbulenceBehavior final : public ParticleBehavior
    {
    public:
        f32 strength = 1.0f;
        f32 frequency = 1.0f;
        f32 speed = 1.0f;
        [[nodiscard]] BehaviorSupport Support() const noexcept override { return BehaviorSupport::CPUOnly; }
        void DeclareStreams(ParticleStreamContainer& streams) override { streams.EnsureStream(ParticleStreamId::Velocity, StreamElementType::Float3); }
        void Update(ParticleStreamContainer& streams, ParticleUpdateContext& ctx) override
        {
            CPUStream<Vector3>* vel = streams.Velocities();
            CPUStream<Vector3>* pos = streams.Positions();
            if (vel == nullptr || pos == nullptr) { return; }
            const f32 scroll = ctx.totalTime * speed;
            for (i32 i = 0; i < streams.aliveCount; ++i)
            {
                const Vector3 p = (*pos)[i] * frequency + Vector3{ scroll, scroll, scroll };
                const Vector3 noise{ Sin(p.y * 1.7f + p.z), Sin(p.z * 1.3f + p.x), Sin(p.x * 1.9f + p.y) };   // cheap pseudo-noise
                (*vel)[i] += noise * (strength * ctx.deltaTime);
            }
        }
    };

    class VortexBehavior final : public ParticleBehavior
    {
    public:
        f32 strength = 1.0f;
        Vector3 center{ 0.0f, 0.0f, 0.0f };
        Vector3 axis{ 0.0f, 1.0f, 0.0f };
        [[nodiscard]] BehaviorSupport Support() const noexcept override { return BehaviorSupport::Both; }
        void DeclareStreams(ParticleStreamContainer& streams) override { streams.EnsureStream(ParticleStreamId::Velocity, StreamElementType::Float3); }
        void Update(ParticleStreamContainer& streams, ParticleUpdateContext& ctx) override
        {
            CPUStream<Vector3>* vel = streams.Velocities();
            CPUStream<Vector3>* pos = streams.Positions();
            if (vel == nullptr || pos == nullptr) { return; }
            const Vector3 a = (LengthSquared(axis) > 1e-6f) ? Normalized(axis) : Vector3::UnitY;
            for (i32 i = 0; i < streams.aliveCount; ++i)
            {
                const Vector3 radial = (*pos)[i] - center;
                const Vector3 tangent = Cross(a, radial);
                const f32 dist = Max(Length(radial), 0.1f);
                (*vel)[i] += tangent * (strength * ctx.deltaTime / dist);
            }
        }
    };

    class AttractorBehavior final : public ParticleBehavior
    {
    public:
        f32 strength = 1.0f;
        Vector3 position{ 0.0f, 0.0f, 0.0f };
        f32 radius = 0.0f;
        [[nodiscard]] BehaviorSupport Support() const noexcept override { return BehaviorSupport::Both; }
        void DeclareStreams(ParticleStreamContainer& streams) override { streams.EnsureStream(ParticleStreamId::Velocity, StreamElementType::Float3); }
        void Update(ParticleStreamContainer& streams, ParticleUpdateContext& ctx) override
        {
            CPUStream<Vector3>* vel = streams.Velocities();
            CPUStream<Vector3>* pos = streams.Positions();
            if (vel == nullptr || pos == nullptr) { return; }
            for (i32 i = 0; i < streams.aliveCount; ++i)
            {
                const Vector3 delta = position - (*pos)[i];
                const f32 dist = Length(delta);
                if (dist < 1e-4f) { continue; }
                f32 s = strength;
                if (radius > 0.0f && dist > radius) { s *= radius / dist; }
                (*vel)[i] += (delta / dist) * (s * ctx.deltaTime);
            }
        }
    };

    class RadialForceBehavior final : public ParticleBehavior
    {
    public:
        f32 strength = 1.0f;
        [[nodiscard]] BehaviorSupport Support() const noexcept override { return BehaviorSupport::Both; }
        void DeclareStreams(ParticleStreamContainer& streams) override { streams.EnsureStream(ParticleStreamId::Velocity, StreamElementType::Float3); }
        void Update(ParticleStreamContainer& streams, ParticleUpdateContext& ctx) override
        {
            CPUStream<Vector3>* vel = streams.Velocities();
            CPUStream<Vector3>* pos = streams.Positions();
            if (vel == nullptr || pos == nullptr) { return; }
            for (i32 i = 0; i < streams.aliveCount; ++i)
            {
                const Vector3 delta = (*pos)[i] - ctx.emitterPosition;
                if (LengthSquared(delta) < 1e-8f) { continue; }
                (*vel)[i] += Normalized(delta) * (strength * ctx.deltaTime);
            }
        }
    };

    // ---- Over-lifetime behaviors (sample t = GetLifeRatio) -----------------------------------

    class ColorOverLifetimeBehavior final : public ParticleBehavior
    {
    public:
        ParticleCurveColor curve;
        [[nodiscard]] BehaviorSupport Support() const noexcept override { return BehaviorSupport::Both; }
        void DeclareStreams(ParticleStreamContainer& streams) override { streams.EnsureStream(ParticleStreamId::Color, StreamElementType::Float4); }
        void Update(ParticleStreamContainer& streams, ParticleUpdateContext&) override
        {
            if (!curve.IsActive()) { return; }
            CPUStream<Vector4>* col = streams.Colors();
            if (col == nullptr) { return; }
            for (i32 i = 0; i < streams.aliveCount; ++i) { (*col)[i] = curve.Evaluate(streams.GetLifeRatio(i)); }
        }
    };

    class AlphaOverLifetimeBehavior final : public ParticleBehavior
    {
    public:
        ParticleCurveFloat curve;
        [[nodiscard]] BehaviorSupport Support() const noexcept override { return BehaviorSupport::Both; }
        void DeclareStreams(ParticleStreamContainer& streams) override { streams.EnsureStream(ParticleStreamId::Color, StreamElementType::Float4); }
        void Update(ParticleStreamContainer& streams, ParticleUpdateContext&) override
        {
            if (!curve.IsActive()) { return; }
            CPUStream<Vector4>* col = streams.Colors();
            if (col == nullptr) { return; }
            for (i32 i = 0; i < streams.aliveCount; ++i) { (*col)[i].w *= curve.Evaluate(streams.GetLifeRatio(i)); }
        }
    };

    class SizeOverLifetimeBehavior final : public ParticleBehavior
    {
    public:
        ParticleCurveVector2 curve;
        [[nodiscard]] BehaviorSupport Support() const noexcept override { return BehaviorSupport::Both; }
        void DeclareStreams(ParticleStreamContainer& streams) override { streams.EnsureStream(ParticleStreamId::Size, StreamElementType::Float2); }
        void Update(ParticleStreamContainer& streams, ParticleUpdateContext&) override
        {
            if (!curve.IsActive()) { return; }
            CPUStream<Vector2>* size = streams.Sizes();
            if (size == nullptr) { return; }
            for (i32 i = 0; i < streams.aliveCount; ++i) { (*size)[i] = curve.Evaluate(streams.GetLifeRatio(i)); }
        }
    };

    class RotationOverLifetimeBehavior final : public ParticleBehavior
    {
    public:
        ParticleCurveFloat curve;
        [[nodiscard]] BehaviorSupport Support() const noexcept override { return BehaviorSupport::Both; }
        void DeclareStreams(ParticleStreamContainer& streams) override
        {
            streams.EnsureStream(ParticleStreamId::Rotation, StreamElementType::Float);
            streams.EnsureStream(ParticleStreamId::RotationSpeed, StreamElementType::Float);
        }
        void Update(ParticleStreamContainer& streams, ParticleUpdateContext& ctx) override
        {
            CPUStream<f32>* rot = streams.Rotations();
            CPUStream<f32>* spd = streams.RotationSpeeds();
            if (rot == nullptr || spd == nullptr) { return; }
            const bool active = curve.IsActive();
            for (i32 i = 0; i < streams.aliveCount; ++i)
            {
                const f32 scale = active ? curve.Evaluate(streams.GetLifeRatio(i)) : 1.0f;
                (*rot)[i] += (*spd)[i] * scale * ctx.deltaTime;
            }
        }
    };

    class SpeedOverLifetimeBehavior final : public ParticleBehavior
    {
    public:
        ParticleCurveFloat curve;
        [[nodiscard]] BehaviorSupport Support() const noexcept override { return BehaviorSupport::Both; }
        void DeclareStreams(ParticleStreamContainer& streams) override
        {
            streams.EnsureStream(ParticleStreamId::Velocity, StreamElementType::Float3);
            streams.EnsureStream(ParticleStreamId::StartVelocity, StreamElementType::Float3);
        }
        void Update(ParticleStreamContainer& streams, ParticleUpdateContext&) override
        {
            if (!curve.IsActive()) { return; }
            CPUStream<Vector3>* vel = streams.Velocities();
            CPUStream<Vector3>* start = streams.StartVelocities();
            if (vel == nullptr || start == nullptr) { return; }
            for (i32 i = 0; i < streams.aliveCount; ++i)
            {
                const Vector3 v = (*vel)[i];
                const f32 len = Length(v);
                if (len < 1e-6f) { continue; }
                const f32 target = Length((*start)[i]) * curve.Evaluate(streams.GetLifeRatio(i));
                (*vel)[i] = (v / len) * target;
            }
        }
    };

    // ---- CPU simulator -----------------------------------------------------------------------

    class CPUSimulator final : public ParticleSimulator
    {
    public:
        void Simulate(ParticleStreamContainer& streams, const Array<UniquePtr<ParticleBehavior>>& behaviors, ParticleUpdateContext& ctx) override
        {
            for (usize i = 0; i < behaviors.Size(); ++i) { behaviors[i]->Update(streams, ctx); }
        }
        i32 CompactDead(ParticleStreamContainer& streams) override { return streams.CompactDead(); }
    };

    // ---- Runtime type-id registry ------------------------------------------------------------
    // Reconstruct a module from its stable string id (used later by the cooked-resource factory).
    // Unknown ids return null (tolerant - a dropped module type is simply skipped).

    [[nodiscard]] UniquePtr<ParticleInitializer> CreateInitializer(StringView id)
    {
        IAllocator& a = DefaultAllocator();
        if (id == StringView(u8"Position"))        { return MakeUnique<PositionInitializer>(a); }
        if (id == StringView(u8"Velocity"))        { return MakeUnique<VelocityInitializer>(a); }
        if (id == StringView(u8"Lifetime"))        { return MakeUnique<LifetimeInitializer>(a); }
        if (id == StringView(u8"Color"))           { return MakeUnique<ColorInitializer>(a); }
        if (id == StringView(u8"Size"))            { return MakeUnique<SizeInitializer>(a); }
        if (id == StringView(u8"Rotation"))        { return MakeUnique<RotationInitializer>(a); }
        if (id == StringView(u8"MeshOrientation")) { return MakeUnique<MeshOrientationInitializer>(a); }
        return {};
    }

    [[nodiscard]] UniquePtr<ParticleBehavior> CreateBehavior(StringView id)
    {
        IAllocator& a = DefaultAllocator();
        if (id == StringView(u8"Gravity"))             { return MakeUnique<GravityBehavior>(a); }
        if (id == StringView(u8"Drag"))                { return MakeUnique<DragBehavior>(a); }
        if (id == StringView(u8"Wind"))                { return MakeUnique<WindBehavior>(a); }
        if (id == StringView(u8"Turbulence"))          { return MakeUnique<TurbulenceBehavior>(a); }
        if (id == StringView(u8"Vortex"))              { return MakeUnique<VortexBehavior>(a); }
        if (id == StringView(u8"Attractor"))           { return MakeUnique<AttractorBehavior>(a); }
        if (id == StringView(u8"RadialForce"))         { return MakeUnique<RadialForceBehavior>(a); }
        if (id == StringView(u8"ColorOverLifetime"))   { return MakeUnique<ColorOverLifetimeBehavior>(a); }
        if (id == StringView(u8"AlphaOverLifetime"))   { return MakeUnique<AlphaOverLifetimeBehavior>(a); }
        if (id == StringView(u8"SizeOverLifetime"))    { return MakeUnique<SizeOverLifetimeBehavior>(a); }
        if (id == StringView(u8"RotationOverLifetime")){ return MakeUnique<RotationOverLifetimeBehavior>(a); }
        if (id == StringView(u8"SpeedOverLifetime"))   { return MakeUnique<SpeedOverLifetimeBehavior>(a); }
        return {};
    }
}
