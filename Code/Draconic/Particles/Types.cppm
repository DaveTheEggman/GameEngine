// draconic.particles:types - the particle value primitives, ported from Sedulous.Particles
// (ParticleTypes.bf, RangeValue.bf, ParticleCurve.bf, EmissionShape.bf, ParticleEvent.bf,
// ParticleBehavior.bf's update context). Pure value types over Core math. Fields adapt to
// Draconic's camelCase convention; methods stay PascalCase.

module;
#include "Core/Prelude.h"

export module draconic.particles:types;

import draconic.core;

using namespace draconic::core;

export namespace draconic::particles
{
    // ---- Simulation / space / render mode enums (ParticleTypes.bf) ----------------------------

    // Where a system's particle update runs. GPU is stubbed for now (Phase 6); Auto resolves
    // to GPU only when every behavior supports it and the system is large.
    enum class SimulationMode : u8 { CPU, GPU, Auto };

    // Whether a behavior can run on CPU, GPU, or either. Drives Auto/GPU resolution.
    enum class BehaviorSupport : u8 { CPUOnly, GPUOnly, Both };

    // Simulation coordinate space: World (particles independent of the emitter after spawn) or
    // Local (particles follow the emitter transform).
    enum class ParticleSpace : u8 { World, Local };

    // Blend mode for a system's draw. Consumed by the render layer.
    enum class ParticleBlendMode : u8 { Alpha, Additive, Premultiplied, Multiply };

    // How a system's particles are rendered. Not a polymorphic module (à la Sedulous) - a tag
    // the render extractor branches on. Light (an ez-style extension beyond Sedulous) contributes a
    // point light per particle to the clustered-forward light list, and also draws the billboard glow.
    enum class ParticleRenderMode : u8 { Billboard, StretchedBillboard, HorizontalBillboard, VerticalBillboard, Mesh, Trail, Light };

    // ---- Range values (RangeValue.bf) --------------------------------------------------------
    // A min/max pair sampled by a single shared t in [0,1) (Min<->Max is a diagonal lerp, NOT
    // per-component independent - matches Sedulous's Evaluate((float)rng.NextDouble())).

    struct RangeFloat
    {
        f32 min = 0.0f;
        f32 max = 0.0f;
        constexpr RangeFloat() noexcept = default;
        constexpr explicit RangeFloat(f32 value) noexcept : min(value), max(value) {}
        constexpr RangeFloat(f32 mn, f32 mx) noexcept : min(mn), max(mx) {}
        [[nodiscard]] constexpr bool IsConstant() const noexcept { return min == max; }
        [[nodiscard]] constexpr f32 Evaluate(f32 t) const noexcept { return min + (max - min) * t; }
        [[nodiscard]] static constexpr RangeFloat Constant(f32 v) noexcept { return RangeFloat(v); }
        [[nodiscard]] static constexpr RangeFloat Range(f32 mn, f32 mx) noexcept { return RangeFloat(mn, mx); }
    };

    struct RangeVector2
    {
        Vector2 min{ 0.0f, 0.0f };
        Vector2 max{ 0.0f, 0.0f };
        constexpr RangeVector2() noexcept = default;
        constexpr explicit RangeVector2(Vector2 value) noexcept : min(value), max(value) {}
        constexpr RangeVector2(Vector2 mn, Vector2 mx) noexcept : min(mn), max(mx) {}
        [[nodiscard]] constexpr Vector2 Evaluate(f32 t) const noexcept { return min + (max - min) * t; }
        [[nodiscard]] static constexpr RangeVector2 Constant(Vector2 v) noexcept { return RangeVector2(v); }
    };

    struct RangeColor
    {
        Vector4 min{ 1.0f, 1.0f, 1.0f, 1.0f };
        Vector4 max{ 1.0f, 1.0f, 1.0f, 1.0f };
        constexpr RangeColor() noexcept = default;
        constexpr explicit RangeColor(Vector4 value) noexcept : min(value), max(value) {}
        constexpr RangeColor(Vector4 mn, Vector4 mx) noexcept : min(mn), max(mx) {}
        [[nodiscard]] constexpr Vector4 Evaluate(f32 t) const noexcept { return min + (max - min) * t; }
        [[nodiscard]] static constexpr RangeColor Constant(Vector4 v) noexcept { return RangeColor(v); }
    };

    // ---- Curves (ParticleCurve.bf) -----------------------------------------------------------
    // Fixed 8-key curves sampled by normalized lifetime. Float/Vector2 are cubic Hermite;
    // Color is linear (it doubles as the color gradient). IsActive() gates the *OverLifetime
    // behaviors (an empty curve is a no-op).

    inline constexpr i32 kMaxCurveKeys = 8;

    // Cubic Hermite basis: p0 at t=0, p1 at t=1, tangents m0/m1 (already scaled by segment length).
    [[nodiscard]] constexpr f32 HermiteFloat(f32 p0, f32 m0, f32 p1, f32 m1, f32 t) noexcept
    {
        const f32 t2 = t * t;
        const f32 t3 = t2 * t;
        return (2.0f * t3 - 3.0f * t2 + 1.0f) * p0
             + (t3 - 2.0f * t2 + t) * m0
             + (-2.0f * t3 + 3.0f * t2) * p1
             + (t3 - t2) * m1;
    }

    struct CurveKeyFloat
    {
        f32 time = 0.0f;
        f32 value = 0.0f;
        f32 tangentIn = 0.0f;
        f32 tangentOut = 0.0f;
    };

    struct ParticleCurveFloat
    {
        CurveKeyFloat keys[kMaxCurveKeys]{};
        i32 keyCount = 0;

        [[nodiscard]] constexpr bool IsActive() const noexcept { return keyCount > 0; }

        [[nodiscard]] f32 Evaluate(f32 t) const noexcept
        {
            if (keyCount <= 0) { return 0.0f; }
            if (keyCount == 1) { return keys[0].value; }
            if (t <= keys[0].time) { return keys[0].value; }
            if (t >= keys[keyCount - 1].time) { return keys[keyCount - 1].value; }
            i32 i = 0;
            while (i < keyCount - 1 && t > keys[i + 1].time) { ++i; }
            const CurveKeyFloat& k0 = keys[i];
            const CurveKeyFloat& k1 = keys[i + 1];
            const f32 segLen = k1.time - k0.time;
            const f32 localT = (segLen > 1e-6f) ? (t - k0.time) / segLen : 0.0f;
            return HermiteFloat(k0.value, k0.tangentOut * segLen, k1.value, k1.tangentIn * segLen, localT);
        }

        // Sorted insert (drops silently once full - matches the fixed 8-key cap).
        bool AddKey(f32 time, f32 value, f32 tangentIn = 0.0f, f32 tangentOut = 0.0f) noexcept
        {
            if (keyCount >= kMaxCurveKeys) { return false; }
            i32 idx = keyCount;
            while (idx > 0 && keys[idx - 1].time > time) { keys[idx] = keys[idx - 1]; --idx; }
            keys[idx] = CurveKeyFloat{ time, value, tangentIn, tangentOut };
            ++keyCount;
            return true;
        }

        [[nodiscard]] static ParticleCurveFloat Constant(f32 value) noexcept
        {
            ParticleCurveFloat c; c.AddKey(0.0f, value); return c;
        }
        [[nodiscard]] static ParticleCurveFloat Linear(f32 a, f32 b) noexcept
        {
            ParticleCurveFloat c; c.AddKey(0.0f, a, 0.0f, b - a); c.AddKey(1.0f, b, b - a, 0.0f); return c;
        }
        [[nodiscard]] static ParticleCurveFloat EaseIn(f32 a, f32 b) noexcept
        {
            ParticleCurveFloat c; c.AddKey(0.0f, a, 0.0f, 0.0f); c.AddKey(1.0f, b, 2.0f * (b - a), 0.0f); return c;
        }
        [[nodiscard]] static ParticleCurveFloat EaseOut(f32 a, f32 b) noexcept
        {
            ParticleCurveFloat c; c.AddKey(0.0f, a, 0.0f, 2.0f * (b - a)); c.AddKey(1.0f, b, 0.0f, 0.0f); return c;
        }
        [[nodiscard]] static ParticleCurveFloat FadeOut(f32 value, f32 fadeStart = 0.75f) noexcept
        {
            ParticleCurveFloat c; c.AddKey(0.0f, value); c.AddKey(fadeStart, value); c.AddKey(1.0f, 0.0f); return c;
        }
        [[nodiscard]] static ParticleCurveFloat PeakAt(f32 peakValue, f32 peakTime = 0.3f) noexcept
        {
            ParticleCurveFloat c; c.AddKey(0.0f, 0.0f); c.AddKey(peakTime, peakValue); c.AddKey(1.0f, 0.0f); return c;
        }
    };

    struct CurveKeyColor
    {
        f32 time = 0.0f;
        Vector4 color{ 1.0f, 1.0f, 1.0f, 1.0f };
    };

    struct ParticleCurveColor
    {
        CurveKeyColor keys[kMaxCurveKeys]{};
        i32 keyCount = 0;

        [[nodiscard]] constexpr bool IsActive() const noexcept { return keyCount > 0; }

        [[nodiscard]] Vector4 Evaluate(f32 t) const noexcept
        {
            if (keyCount <= 0) { return Vector4{ 1.0f, 1.0f, 1.0f, 1.0f }; }
            if (keyCount == 1) { return keys[0].color; }
            if (t <= keys[0].time) { return keys[0].color; }
            if (t >= keys[keyCount - 1].time) { return keys[keyCount - 1].color; }
            i32 i = 0;
            while (i < keyCount - 1 && t > keys[i + 1].time) { ++i; }
            const CurveKeyColor& k0 = keys[i];
            const CurveKeyColor& k1 = keys[i + 1];
            const f32 segLen = k1.time - k0.time;
            const f32 localT = (segLen > 1e-6f) ? (t - k0.time) / segLen : 0.0f;
            return k0.color + (k1.color - k0.color) * localT;   // linear
        }

        bool AddKey(f32 time, Vector4 color) noexcept
        {
            if (keyCount >= kMaxCurveKeys) { return false; }
            i32 idx = keyCount;
            while (idx > 0 && keys[idx - 1].time > time) { keys[idx] = keys[idx - 1]; --idx; }
            keys[idx] = CurveKeyColor{ time, color };
            ++keyCount;
            return true;
        }

        [[nodiscard]] static ParticleCurveColor Constant(Vector4 color) noexcept
        {
            ParticleCurveColor c; c.AddKey(0.0f, color); return c;
        }
        [[nodiscard]] static ParticleCurveColor Linear(Vector4 a, Vector4 b) noexcept
        {
            ParticleCurveColor c; c.AddKey(0.0f, a); c.AddKey(1.0f, b); return c;
        }
        // Constant RGB with alpha faded to 0 over [fadeStart, 1].
        [[nodiscard]] static ParticleCurveColor FadeAlpha(Vector4 color, f32 fadeStart = 0.75f) noexcept
        {
            ParticleCurveColor c;
            c.AddKey(0.0f, color);
            c.AddKey(fadeStart, color);
            c.AddKey(1.0f, Vector4{ color.x, color.y, color.z, 0.0f });
            return c;
        }
    };

    struct ParticleCurveVector2
    {
        f32 times[kMaxCurveKeys]{};
        Vector2 values[kMaxCurveKeys]{};
        Vector2 tangentsIn[kMaxCurveKeys]{};
        Vector2 tangentsOut[kMaxCurveKeys]{};
        i32 keyCount = 0;

        [[nodiscard]] constexpr bool IsActive() const noexcept { return keyCount > 0; }

        [[nodiscard]] Vector2 Evaluate(f32 t) const noexcept
        {
            if (keyCount <= 0) { return Vector2{ 0.0f, 0.0f }; }
            if (keyCount == 1) { return values[0]; }
            if (t <= times[0]) { return values[0]; }
            if (t >= times[keyCount - 1]) { return values[keyCount - 1]; }
            i32 i = 0;
            while (i < keyCount - 1 && t > times[i + 1]) { ++i; }
            const f32 segLen = times[i + 1] - times[i];
            const f32 localT = (segLen > 1e-6f) ? (t - times[i]) / segLen : 0.0f;
            return Vector2{
                HermiteFloat(values[i].x, tangentsOut[i].x * segLen, values[i + 1].x, tangentsIn[i + 1].x * segLen, localT),
                HermiteFloat(values[i].y, tangentsOut[i].y * segLen, values[i + 1].y, tangentsIn[i + 1].y * segLen, localT),
            };
        }

        bool AddKey(f32 time, Vector2 value, Vector2 tangentIn = Vector2{ 0.0f, 0.0f }, Vector2 tangentOut = Vector2{ 0.0f, 0.0f }) noexcept
        {
            if (keyCount >= kMaxCurveKeys) { return false; }
            i32 idx = keyCount;
            while (idx > 0 && times[idx - 1] > time) {
                times[idx] = times[idx - 1]; values[idx] = values[idx - 1];
                tangentsIn[idx] = tangentsIn[idx - 1]; tangentsOut[idx] = tangentsOut[idx - 1]; --idx;
            }
            times[idx] = time; values[idx] = value; tangentsIn[idx] = tangentIn; tangentsOut[idx] = tangentOut;
            ++keyCount;
            return true;
        }

        [[nodiscard]] static ParticleCurveVector2 Constant(Vector2 value) noexcept
        {
            ParticleCurveVector2 c; c.AddKey(0.0f, value); return c;
        }
        [[nodiscard]] static ParticleCurveVector2 Linear(Vector2 a, Vector2 b) noexcept
        {
            ParticleCurveVector2 c; c.AddKey(0.0f, a); c.AddKey(1.0f, b); return c;
        }
    };

    // ---- Emission shape (EmissionShape.bf) ---------------------------------------------------
    // A tagged spawn volume: Sample() returns a spawn-local position and an outward direction.
    // (Faithful subset; Sedulous's exact per-shape fields to be reconciled as shapes are added.)

    enum class EmissionShapeType : u8 { Point, Sphere, Hemisphere, Box, Cone, Ring };

    struct EmissionShape
    {
        EmissionShapeType type = EmissionShapeType::Point;
        f32 radius = 1.0f;               // Sphere/Hemisphere/Cone/Ring
        Vector3 extents{ 1.0f, 1.0f, 1.0f };  // Box half-extents
        f32 angle = 0.7853982f;          // Cone half-angle (radians)
        bool emitFromShell = false;      // Sphere/Ring: surface vs volume

        // Returns a position (spawn-local, before the emitter offset) and a normalized direction.
        void Sample(Random& rng, Vector3& outPosition, Vector3& outDirection) const noexcept
        {
            switch (type)
            {
                case EmissionShapeType::Sphere:
                case EmissionShapeType::Hemisphere:
                {
                    const f32 cosMin = (type == EmissionShapeType::Hemisphere) ? 0.0f : -1.0f;
                    const f32 z = rng.NextFloat(cosMin, 1.0f);
                    const f32 phi = rng.NextFloat(0.0f, 6.2831853f);
                    const f32 r = Sqrt(1.0f - z * z);
                    const Vector3 dir{ r * Cos(phi), r * Sin(phi), z };
                    const f32 dist = emitFromShell ? radius : radius * Pow(rng.NextFloat(), 1.0f / 3.0f);   // volume-uniform
                    outPosition = dir * dist;
                    outDirection = dir;
                    break;
                }
                case EmissionShapeType::Box:
                {
                    outPosition = Vector3{ rng.NextFloat(-extents.x, extents.x),
                                           rng.NextFloat(-extents.y, extents.y),
                                           rng.NextFloat(-extents.z, extents.z) };
                    outDirection = (LengthSquared(outPosition) > 1e-6f) ? Normalized(outPosition) : Vector3::UnitY;
                    break;
                }
                case EmissionShapeType::Cone:
                {
                    const f32 phi = rng.NextFloat(0.0f, 6.2831853f);
                    const f32 rr = radius * Sqrt(rng.NextFloat());
                    outPosition = Vector3{ rr * Cos(phi), 0.0f, rr * Sin(phi) };
                    const f32 spread = Sin(angle);
                    outDirection = Normalized(Vector3{ spread * Cos(phi), Cos(angle), spread * Sin(phi) });
                    break;
                }
                case EmissionShapeType::Ring:
                {
                    const f32 phi = rng.NextFloat(0.0f, 6.2831853f);
                    outPosition = Vector3{ radius * Cos(phi), 0.0f, radius * Sin(phi) };
                    outDirection = (LengthSquared(outPosition) > 1e-6f) ? Normalized(outPosition) : Vector3::UnitY;
                    break;
                }
                case EmissionShapeType::Point:
                default:
                    outPosition = Vector3::Zero;
                    outDirection = Vector3::UnitY;
                    break;
            }
        }

        [[nodiscard]] static EmissionShape Point() noexcept { return EmissionShape{}; }
        [[nodiscard]] static EmissionShape Sphere(f32 radius, bool shell = false) noexcept
        {
            EmissionShape s; s.type = EmissionShapeType::Sphere; s.radius = radius; s.emitFromShell = shell; return s;
        }
        [[nodiscard]] static EmissionShape Box(Vector3 extents) noexcept
        {
            EmissionShape s; s.type = EmissionShapeType::Box; s.extents = extents; return s;
        }
        [[nodiscard]] static EmissionShape Cone(f32 radius, f32 angle) noexcept
        {
            EmissionShape s; s.type = EmissionShapeType::Cone; s.radius = radius; s.angle = angle; return s;
        }
    };

    // ---- Sub-emitter events (ParticleEvent.bf) -----------------------------------------------

    enum class ParticleEventType : u8 { OnBirth, OnDeath };

    struct ParticleEvent
    {
        Vector3 position{ 0.0f, 0.0f, 0.0f };
        Vector3 velocity{ 0.0f, 0.0f, 0.0f };
        Vector4 color{ 1.0f, 1.0f, 1.0f, 1.0f };
    };

    struct SubEmitterLink
    {
        ParticleEventType trigger = ParticleEventType::OnDeath;
        i32 childSystemIndex = -1;
        i32 spawnCount = 1;
        f32 probability = 1.0f;
        bool inheritPosition = true;
        bool inheritVelocity = false;
        f32 velocityInheritFactor = 0.5f;
        bool inheritColor = false;

        [[nodiscard]] static SubEmitterLink Default() noexcept { return SubEmitterLink{}; }
    };

    // ---- Per-frame update context (ParticleBehavior.bf) --------------------------------------
    // Passed by reference to every behavior each frame. Rng is borrowed (owned by the system).

    struct ParticleUpdateContext
    {
        f32 totalTime = 0.0f;
        f32 deltaTime = 0.0f;
        Vector3 emitterPosition{ 0.0f, 0.0f, 0.0f };
        Random* rng = nullptr;
    };
}
