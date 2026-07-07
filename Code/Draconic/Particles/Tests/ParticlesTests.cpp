// draconic.particles - CPU runtime coverage: value types (ranges/curves/emission shapes), the
// SoA stream container (lazy alloc / typed access / swap-remove / compaction), the modules
// (initializers + behaviors), and the effect/system Update loop (spawn, integrate, age, die),
// plus sub-emitters, LOD, and determinism. No GPU/renderer.
#include <doctest/doctest.h>
#include "Core/Prelude.h"

import draconic.core;
import draconic.particles;

using namespace draconic::core;
namespace px = draconic::particles;

// ---- value types -------------------------------------------------------------------------------

TEST_CASE("RangeFloat/RangeColor: diagonal lerp between min and max")
{
    const px::RangeFloat r{ 2.0f, 6.0f };
    CHECK(r.Evaluate(0.0f) == doctest::Approx(2.0f));
    CHECK(r.Evaluate(1.0f) == doctest::Approx(6.0f));
    CHECK(r.Evaluate(0.5f) == doctest::Approx(4.0f));
    CHECK(px::RangeFloat::Constant(3.0f).IsConstant());

    const px::RangeColor c{ Vector4{ 0, 0, 0, 0 }, Vector4{ 1, 2, 3, 4 } };
    const Vector4 mid = c.Evaluate(0.5f);
    CHECK(mid.x == doctest::Approx(0.5f));
    CHECK(mid.w == doctest::Approx(2.0f));
}

TEST_CASE("ParticleCurveFloat: endpoints, Constant, Linear, FadeOut")
{
    CHECK_FALSE(px::ParticleCurveFloat{}.IsActive());
    CHECK(px::ParticleCurveFloat::Constant(5.0f).Evaluate(0.37f) == doctest::Approx(5.0f));

    const px::ParticleCurveFloat lin = px::ParticleCurveFloat::Linear(0.0f, 10.0f);
    CHECK(lin.Evaluate(0.0f) == doctest::Approx(0.0f));
    CHECK(lin.Evaluate(1.0f) == doctest::Approx(10.0f));
    CHECK(lin.Evaluate(-1.0f) == doctest::Approx(0.0f));   // clamps to first key
    CHECK(lin.Evaluate(2.0f) == doctest::Approx(10.0f));   // clamps to last key
    CHECK(lin.Evaluate(0.5f) > 0.0f);
    CHECK(lin.Evaluate(0.5f) < 10.0f);

    const px::ParticleCurveFloat fade = px::ParticleCurveFloat::FadeOut(1.0f, 0.75f);
    CHECK(fade.Evaluate(0.0f) == doctest::Approx(1.0f));
    CHECK(fade.Evaluate(1.0f) == doctest::Approx(0.0f));
    CHECK(fade.Evaluate(0.5f) == doctest::Approx(1.0f));   // constant until fadeStart
}

TEST_CASE("EmissionShape: Sphere samples inside radius, Point is origin")
{
    Random rng(1234);
    Vector3 pos, dir;

    px::EmissionShape::Point().Sample(rng, pos, dir);
    CHECK(LengthSquared(pos) == doctest::Approx(0.0f));

    const px::EmissionShape sphere = px::EmissionShape::Sphere(2.0f);
    for (int i = 0; i < 200; ++i)
    {
        sphere.Sample(rng, pos, dir);
        CHECK(Length(pos) <= doctest::Approx(2.0f).epsilon(0.01));
        CHECK(Length(dir) == doctest::Approx(1.0f).epsilon(0.01));
    }
}

// ---- stream container --------------------------------------------------------------------------

TEST_CASE("ParticleStreamContainer: core streams present, others lazy, typed access")
{
    px::ParticleStreamContainer streams(64);
    CHECK(streams.Positions() != nullptr);
    CHECK(streams.Ages() != nullptr);
    CHECK(streams.Lifetimes() != nullptr);
    CHECK(streams.Velocities() == nullptr);   // not allocated yet

    streams.EnsureStream(px::ParticleStreamId::Velocity, px::StreamElementType::Float3);
    CHECK(streams.Velocities() != nullptr);

    // Idempotent: a second EnsureStream keeps the same stream object.
    px::ParticleStream* before = streams.GetStream(px::ParticleStreamId::Velocity);
    streams.EnsureStream(px::ParticleStreamId::Velocity, px::StreamElementType::Float3);
    CHECK(streams.GetStream(px::ParticleStreamId::Velocity) == before);

    // Wrong element type -> null (checked cast).
    CHECK(streams.GetCPUStream<f32>(px::ParticleStreamId::Velocity) == nullptr);
}

TEST_CASE("ParticleStreamContainer: swap-remove keeps arrays dense, CompactDead drops aged")
{
    px::ParticleStreamContainer streams(16);
    px::CPUStream<Vector3>* pos = streams.Positions();
    px::CPUStream<f32>* ages = streams.Ages();
    px::CPUStream<f32>* lifetimes = streams.Lifetimes();

    for (i32 i = 0; i < 5; ++i) { (*pos)[i] = Vector3{ static_cast<f32>(i), 0, 0 }; (*ages)[i] = 0.0f; (*lifetimes)[i] = 1.0f; }
    streams.aliveCount = 5;

    // Kill index 1: last (index 4) swaps into slot 1.
    streams.SwapRemove(1);
    CHECK(streams.aliveCount == 4);
    CHECK((*pos)[1].x == doctest::Approx(4.0f));

    // Age two out and compact.
    (*ages)[0] = 2.0f; (*ages)[2] = 2.0f;
    const i32 removed = streams.CompactDead();
    CHECK(removed == 2);
    CHECK(streams.aliveCount == 2);

    CHECK(streams.GetLifeRatio(0) == doctest::Approx(0.0f));
}

// ---- modules -----------------------------------------------------------------------------------

TEST_CASE("GravityBehavior accelerates velocity down; AlphaOverLifetime fades alpha")
{
    px::ParticleStreamContainer streams(8);
    streams.EnsureStream(px::ParticleStreamId::Velocity, px::StreamElementType::Float3);
    streams.EnsureStream(px::ParticleStreamId::Color, px::StreamElementType::Float4);
    streams.aliveCount = 1;
    (*streams.Velocities())[0] = Vector3{ 0, 0, 0 };
    (*streams.Colors())[0] = Vector4{ 1, 1, 1, 1 };
    (*streams.Ages())[0] = 0.5f;
    (*streams.Lifetimes())[0] = 1.0f;

    Random rng(1);
    px::ParticleUpdateContext ctx{ 0.0f, 0.5f, Vector3::Zero, &rng };

    px::GravityBehavior gravity;
    gravity.Update(streams, ctx);
    CHECK((*streams.Velocities())[0].y < 0.0f);   // pulled downward

    px::AlphaOverLifetimeBehavior alpha;
    alpha.curve = px::ParticleCurveFloat::Linear(1.0f, 0.0f);
    alpha.Update(streams, ctx);
    CHECK((*streams.Colors())[0].w < 1.0f);        // alpha reduced at t=0.5
}

// ---- effect / system Update loop ---------------------------------------------------------------

namespace
{
    // Builds a simple upward fountain: continuous emission, 2s life, gravity.
    void BuildFountain(px::ParticleSystem& sys, f32 rate = 100.0f)
    {
        sys.AddInitializer<px::LifetimeInitializer>().lifetime = px::RangeFloat(2.0f, 2.0f);
        sys.AddInitializer<px::VelocityInitializer>().baseVelocity = Vector3{ 0, 5, 0 };
        sys.AddInitializer<px::SizeInitializer>();
        sys.AddInitializer<px::ColorInitializer>();
        sys.AddBehavior<px::GravityBehavior>();
        sys.emitter.mode = px::EmissionMode::Continuous;
        sys.emitter.spawnRate = rate;
    }
}

TEST_CASE("ParticleSystem: continuous emission spawns, integrates, and ages out")
{
    px::ParticleSystem sys(10000);
    BuildFountain(sys, 100.0f);

    // 0.1s at 100/s -> ~10 particles.
    sys.Update(0.1f);
    CHECK(sys.AliveCount() >= 9);
    CHECK(sys.AliveCount() <= 11);

    // Velocity carried the particles upward (position integrated).
    const px::CPUStream<Vector3>* pos = sys.Streams().Positions();
    CHECK((*pos)[0].y > 0.0f);

    // Run well past the 2s lifetime with emission off -> everything dies.
    sys.emitter.isEmitting = false;
    for (int i = 0; i < 300; ++i) { sys.Update(0.016f); }
    CHECK(sys.AliveCount() == 0);
}

TEST_CASE("ParticleSystem: never exceeds MaxParticles")
{
    px::ParticleSystem sys(50);
    BuildFountain(sys, 100000.0f);   // absurd rate
    for (int i = 0; i < 10; ++i) { sys.Update(0.1f); }
    CHECK(sys.AliveCount() <= 50);
}

TEST_CASE("ParticleEmitter: single burst when interval <= 0")
{
    px::ParticleSystem sys(1000);
    sys.AddInitializer<px::LifetimeInitializer>().lifetime = px::RangeFloat(5.0f, 5.0f);
    sys.emitter.mode = px::EmissionMode::Burst;
    sys.emitter.burstCount = 20;
    sys.emitter.burstInterval = 0.0f;   // single burst

    sys.Update(0.016f);
    CHECK(sys.AliveCount() == 20);
    sys.Update(0.016f);
    CHECK(sys.AliveCount() == 20);   // no further bursts
}

TEST_CASE("ParticleSystem: same seed + same input is deterministic")
{
    px::ParticleSystem a(1000, /*seed*/ 42);
    px::ParticleSystem b(1000, /*seed*/ 42);
    BuildFountain(a, 200.0f);
    BuildFountain(b, 200.0f);
    for (int i = 0; i < 20; ++i) { a.Update(0.02f); b.Update(0.02f); }

    REQUIRE(a.AliveCount() == b.AliveCount());
    REQUIRE(a.AliveCount() > 0);
    const px::CPUStream<Vector3>* pa = a.Streams().Positions();
    const px::CPUStream<Vector3>* pb = b.Streams().Positions();
    for (i32 i = 0; i < a.AliveCount(); ++i)
    {
        CHECK((*pa)[i].x == doctest::Approx((*pb)[i].x));
        CHECK((*pa)[i].y == doctest::Approx((*pb)[i].y));
    }
}

TEST_CASE("LOD: beyond cull distance the system stops spawning")
{
    px::ParticleSystem sys(1000);
    BuildFountain(sys, 100.0f);
    sys.lodStartDistance = 10.0f;
    sys.lodCullDistance = 20.0f;
    sys.position = Vector3{ 0, 0, 0 };

    sys.Update(0.1f, /*cameraPos*/ Vector3{ 100, 0, 0 });   // far past cull
    CHECK(sys.LODRateMultiplier() == doctest::Approx(0.0f));
    CHECK(sys.AliveCount() == 0);
}

// ---- sub-emitters ------------------------------------------------------------------------------

TEST_CASE("Sub-emitter: parent death spawns into the child system")
{
    px::ParticleEffect fx(u8"fireworks");

    // System 0: short-lived rockets (die quickly -> emit OnDeath events).
    px::ParticleSystem& rockets = fx.AddSystem(100);
    rockets.AddInitializer<px::LifetimeInitializer>().lifetime = px::RangeFloat(0.05f, 0.05f);
    rockets.emitter.mode = px::EmissionMode::Burst;
    rockets.emitter.burstCount = 4;
    rockets.emitter.burstInterval = 0.0f;

    // System 1: sparks, spawned by rocket deaths (no self-emission).
    px::ParticleSystem& sparks = fx.AddSystem(1000);
    sparks.AddInitializer<px::LifetimeInitializer>().lifetime = px::RangeFloat(1.0f, 1.0f);
    sparks.emitter.isEmitting = false;

    px::SubEmitterLink link = px::SubEmitterLink::Default();
    link.trigger = px::ParticleEventType::OnDeath;
    link.childSystemIndex = 1;
    link.spawnCount = 10;
    link.probability = 1.0f;
    fx.AddSubEmitterLink(link);

    px::ParticleEffectInstance inst(fx);
    inst.Update(0.016f);            // rockets burst (4), still alive
    CHECK(rockets.AliveCount() == 4);
    CHECK(sparks.AliveCount() == 0);

    inst.Update(0.1f);              // rockets age out -> death events -> 4 * 10 sparks
    CHECK(rockets.AliveCount() == 0);
    CHECK(sparks.AliveCount() == 40);
}
