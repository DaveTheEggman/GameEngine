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
    const px::RangeFloat r{2.0f, 6.0f};
    CHECK(r.Evaluate(0.0f) == doctest::Approx(2.0f));
    CHECK(r.Evaluate(1.0f) == doctest::Approx(6.0f));
    CHECK(r.Evaluate(0.5f) == doctest::Approx(4.0f));
    CHECK(px::RangeFloat::Constant(3.0f).IsConstant());

    const px::RangeColor c{Float4{0, 0, 0, 0}, Float4{1, 2, 3, 4}};
    const Float4 mid = c.Evaluate(0.5f);
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
    CHECK(lin.Evaluate(-1.0f) == doctest::Approx(0.0f)); // clamps to first key
    CHECK(lin.Evaluate(2.0f) == doctest::Approx(10.0f)); // clamps to last key
    CHECK(lin.Evaluate(0.5f) > 0.0f);
    CHECK(lin.Evaluate(0.5f) < 10.0f);

    const px::ParticleCurveFloat fade = px::ParticleCurveFloat::FadeOut(1.0f, 0.75f);
    CHECK(fade.Evaluate(0.0f) == doctest::Approx(1.0f));
    CHECK(fade.Evaluate(1.0f) == doctest::Approx(0.0f));
    CHECK(fade.Evaluate(0.5f) == doctest::Approx(1.0f)); // constant until fadeStart
}

TEST_CASE("EmissionShape: Sphere samples inside radius, Point is origin")
{
    Random rng(1234);
    Float3 pos, dir;

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
    CHECK(streams.Velocities() == nullptr); // not allocated yet

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
    px::CPUStream<Float3>* pos = streams.Positions();
    px::CPUStream<f32>* ages = streams.Ages();
    px::CPUStream<f32>* lifetimes = streams.Lifetimes();

    for (i32 i = 0; i < 5; ++i)
    {
        (*pos)[i] = Float3{static_cast<f32>(i), 0, 0};
        (*ages)[i] = 0.0f;
        (*lifetimes)[i] = 1.0f;
    }
    streams.aliveCount = 5;

    // Kill index 1: last (index 4) swaps into slot 1.
    streams.SwapRemove(1);
    CHECK(streams.aliveCount == 4);
    CHECK((*pos)[1].x == doctest::Approx(4.0f));

    // Age two out and compact.
    (*ages)[0] = 2.0f;
    (*ages)[2] = 2.0f;
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
    (*streams.Velocities())[0] = Float3{0, 0, 0};
    (*streams.Colors())[0] = Float4{1, 1, 1, 1};
    (*streams.Ages())[0] = 0.5f;
    (*streams.Lifetimes())[0] = 1.0f;

    Random rng(1);
    px::ParticleUpdateContext ctx{0.0f, 0.5f, Float3::Zero, &rng};

    px::GravityBehavior gravity;
    gravity.Update(streams, ctx);
    CHECK((*streams.Velocities())[0].y < 0.0f); // pulled downward

    px::AlphaOverLifetimeBehavior alpha;
    alpha.curve = px::ParticleCurveFloat::Linear(1.0f, 0.0f);
    alpha.Update(streams, ctx);
    CHECK((*streams.Colors())[0].w < 1.0f); // alpha reduced at t=0.5
}

// ---- effect / system Update loop ---------------------------------------------------------------

namespace
{
    // Builds a simple upward fountain: continuous emission, 2s life, gravity.
    void BuildFountain(px::ParticleSystem& sys, f32 rate = 100.0f)
    {
        sys.AddInitializer<px::LifetimeInitializer>().lifetime = px::RangeFloat(2.0f, 2.0f);
        sys.AddInitializer<px::VelocityInitializer>().baseVelocity = Float3{0, 5, 0};
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
    const px::CPUStream<Float3>* pos = sys.Streams().Positions();
    CHECK((*pos)[0].y > 0.0f);

    // Run well past the 2s lifetime with emission off -> everything dies.
    sys.emitter.isEmitting = false;
    for (int i = 0; i < 300; ++i)
    {
        sys.Update(0.016f);
    }
    CHECK(sys.AliveCount() == 0);
}

TEST_CASE("ParticleSystem: never exceeds MaxParticles")
{
    px::ParticleSystem sys(50);
    BuildFountain(sys, 100000.0f); // absurd rate
    for (int i = 0; i < 10; ++i)
    {
        sys.Update(0.1f);
    }
    CHECK(sys.AliveCount() <= 50);
}

TEST_CASE("ParticleEmitter: single burst when interval <= 0")
{
    px::ParticleSystem sys(1000);
    sys.AddInitializer<px::LifetimeInitializer>().lifetime = px::RangeFloat(5.0f, 5.0f);
    sys.emitter.mode = px::EmissionMode::Burst;
    sys.emitter.burstCount = 20;
    sys.emitter.burstInterval = 0.0f; // single burst

    sys.Update(0.016f);
    CHECK(sys.AliveCount() == 20);
    sys.Update(0.016f);
    CHECK(sys.AliveCount() == 20); // no further bursts
}

TEST_CASE("ParticleSystem: same seed + same input is deterministic")
{
    px::ParticleSystem a(1000, /*seed*/ 42);
    px::ParticleSystem b(1000, /*seed*/ 42);
    BuildFountain(a, 200.0f);
    BuildFountain(b, 200.0f);
    for (int i = 0; i < 20; ++i)
    {
        a.Update(0.02f);
        b.Update(0.02f);
    }

    REQUIRE(a.AliveCount() == b.AliveCount());
    REQUIRE(a.AliveCount() > 0);
    const px::CPUStream<Float3>* pa = a.Streams().Positions();
    const px::CPUStream<Float3>* pb = b.Streams().Positions();
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
    sys.position = Float3{0, 0, 0};

    sys.Update(0.1f, /*cameraPos*/ Float3{100, 0, 0}); // far past cull
    CHECK(sys.LODRateMultiplier() == doctest::Approx(0.0f));
    CHECK(sys.AliveCount() == 0);
}

// ---- sub-emitters ------------------------------------------------------------------------------

// ---- trails -----------------------------------------------------------------------------------

namespace
{
    // A moving Trail-mode system with N burst particles, recording every frame.
    void BuildTrailSystem(px::ParticleSystem& sys, i32 burst, i32 maxPoints)
    {
        sys.renderMode = px::ParticleRenderMode::Trail;
        sys.trail.enabled = true;
        sys.trail.maxPoints = maxPoints;
        sys.trail.recordInterval = 0.0f; // record every frame
        sys.trail.minVertexDistance = 0.0f;
        sys.AddInitializer<px::LifetimeInitializer>().lifetime = px::RangeFloat(100.0f, 100.0f);
        sys.AddInitializer<px::VelocityInitializer>().baseVelocity = Float3{5.0f, 0.0f, 0.0f};
        sys.emitter.mode = px::EmissionMode::Burst;
        sys.emitter.burstCount = burst;
        sys.emitter.burstInterval = 0.0f;
    }
}

TEST_CASE("Trails: ring buffer fills, caps at maxPoints, and records the current position")
{
    px::ParticleSystem sys(100);
    BuildTrailSystem(sys, /*burst*/ 3, /*maxPoints*/ 4);
    CHECK_FALSE(sys.trail.IsActive() == false); // enabled + maxPoints>=2

    sys.Update(0.1f);
    REQUIRE(sys.AliveCount() == 3);
    CHECK(sys.TrailMaxPoints() == 4);
    CHECK(sys.TrailStates()[0].count == 1);

    for (int i = 0; i < 10; ++i)
    {
        sys.Update(0.1f);
    }
    const Span<const px::ParticleTrailState> states = sys.TrailStates();
    REQUIRE(states.Size() == 3);
    for (usize i = 0; i < states.Size(); ++i)
    {
        CHECK(states[i].count == 4);
    } // capped at maxPoints

    // The newest point (at head) is the particle's current position.
    const Span<const px::TrailPoint> points = sys.TrailPoints();
    const px::CPUStream<Float3>* pos = sys.Streams().Positions();
    const i32 head = states[0].head;
    CHECK(points[0 * 4 + head].position.x == doctest::Approx((*pos)[0].x));
}

TEST_CASE("Trails: recordInterval gates how often points are added")
{
    px::ParticleSystem slow(100);
    BuildTrailSystem(slow, 1, 16);
    slow.trail.recordInterval = 0.5f; // one point per 0.5s
    slow.trail.minVertexDistance = 1e9f;

    for (int i = 0; i < 10; ++i)
    {
        slow.Update(0.1f);
    } // 1.0s total
    REQUIRE(slow.AliveCount() == 1);
    // ~1 initial + ~2 interval points (at 0.5s, 1.0s) -> a handful, not 10.
    CHECK(slow.TrailStates()[0].count <= 4);
    CHECK(slow.TrailStates()[0].count >= 2);
}

TEST_CASE("Trails: compaction keeps trail state aligned; dead particles drop cleanly")
{
    px::ParticleSystem sys(100);
    sys.renderMode = px::ParticleRenderMode::Trail;
    sys.trail.enabled = true;
    sys.trail.maxPoints = 8;
    sys.trail.recordInterval = 0.0f;
    sys.trail.minVertexDistance = 0.0f;
    sys.AddInitializer<px::LifetimeInitializer>().lifetime = px::RangeFloat(0.25f, 0.25f); // short
    sys.AddInitializer<px::VelocityInitializer>().baseVelocity = Float3{2.0f, 0.0f, 0.0f};
    sys.emitter.mode = px::EmissionMode::Continuous;
    sys.emitter.spawnRate = 200.0f;

    for (int i = 0; i < 30; ++i)
    {
        sys.Update(0.02f);
    }
    // Steady state: alive particles all have trail states within [0, count<=maxPoints].
    const Span<const px::ParticleTrailState> states = sys.TrailStates();
    CHECK(static_cast<i32>(states.Size()) == sys.AliveCount());
    for (usize i = 0; i < states.Size(); ++i)
    {
        CHECK(states[i].count <= 8);
        CHECK(states[i].count >= 0);
    }

    sys.emitter.isEmitting = false;
    for (int i = 0; i < 30; ++i)
    {
        sys.Update(0.02f);
    }
    CHECK(sys.AliveCount() == 0); // everything ages out, no crash
}

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
    inst.Update(0.016f); // rockets burst (4), still alive
    CHECK(rockets.AliveCount() == 4);
    CHECK(sparks.AliveCount() == 0);

    inst.Update(0.1f); // rockets age out -> death events -> 4 * 10 sparks
    CHECK(rockets.AliveCount() == 0);
    CHECK(sparks.AliveCount() == 40);
}

// ---- new parity+ features ----------------------------------------------------------------------

TEST_CASE("EmissionShape: Circle is a flat XZ disc, Edge is a line on X")
{
    Random rng;
    for (int i = 0; i < 64; ++i)
    {
        Float3 pos, dir;
        px::EmissionShape::Circle(2.0f).Sample(rng, pos, dir);
        CHECK(pos.y == doctest::Approx(0.0f));
        CHECK(Length(Float3{pos.x, 0.0f, pos.z}) <= doctest::Approx(2.0f).epsilon(0.01));

        px::EmissionShape::Edge(3.0f).Sample(rng, pos, dir);
        CHECK(pos.y == doctest::Approx(0.0f));
        CHECK(pos.z == doctest::Approx(0.0f));
        CHECK(pos.x >= -3.01f);
        CHECK(pos.x <= 3.01f);
    }
}

TEST_CASE("EmissionShape: Arc restricts the azimuth to the first quadrant")
{
    Random rng;
    px::EmissionShape s = px::EmissionShape::Circle(1.0f, /*shell*/ true);
    s.arc = 0.25f; // quarter turn -> phi in [0, pi/2] -> x>=0, z>=0
    for (int i = 0; i < 64; ++i)
    {
        Float3 pos, dir;
        s.Sample(rng, pos, dir);
        CHECK(pos.x >= -0.001f);
        CHECK(pos.z >= -0.001f);
    }
}

TEST_CASE("FlipbookSettings: FrameUV walks a grid over lifetime")
{
    px::FlipbookSettings fb;
    fb.enabled = true;
    fb.columns = 4;
    fb.rows = 4;
    fb.overLifetime = true;
    CHECK(fb.IsActive());
    CHECK(fb.FrameCount() == 16);

    const Float4 f0 = fb.FrameUV(0.0f, 0.0f); // frame 0 -> col0,row0
    CHECK(f0.x == doctest::Approx(0.0f));
    CHECK(f0.y == doctest::Approx(0.0f));
    CHECK(f0.z == doctest::Approx(0.25f));
    CHECK(f0.w == doctest::Approx(0.25f));

    const Float4 f8 = fb.FrameUV(0.5f, 0.0f); // frame 8 -> col0,row2
    CHECK(f8.x == doctest::Approx(0.0f));
    CHECK(f8.y == doctest::Approx(0.5f));

    const Float4 fL = fb.FrameUV(0.999f, 0.0f); // frame 15 -> col3,row3
    CHECK(fL.x == doctest::Approx(0.75f));
    CHECK(fL.y == doctest::Approx(0.75f));
}

TEST_CASE("Local space: particles spawn emitter-relative (near origin), not at the world position")
{
    px::ParticleEffect fx(u8"local");
    px::ParticleSystem& sys = fx.AddSystem(100);
    sys.simulationSpace = px::ParticleSpace::Local;
    sys.AddInitializer<px::PositionInitializer>(); // Point shape -> local origin
    sys.AddInitializer<px::LifetimeInitializer>().lifetime = px::RangeFloat(5.0f, 5.0f);
    sys.emitter.mode = px::EmissionMode::Burst;
    sys.emitter.burstCount = 8;

    px::ParticleEffectInstance inst(fx);
    inst.position = Float3{100.0f, 0.0f, 0.0f}; // far from origin
    inst.Update(0.016f);
    REQUIRE(sys.AliveCount() == 8);
    // Stored positions are local (~origin); the emitter transform is applied only at extract.
    CHECK(Length((*sys.Streams().Positions())[0]) < 1.0f);
}

TEST_CASE("Emitter duration: one-shot stops, looping re-arms")
{
    px::ParticleEmitter oneShot;
    oneShot.mode = px::EmissionMode::Continuous;
    oneShot.spawnRate = 100.0f;
    oneShot.duration = 0.1f;
    oneShot.looping = false;
    CHECK(oneShot.CalculateSpawnCount(0.05f) == 5); // inside the window
    CHECK(oneShot.CalculateSpawnCount(0.10f) == 0); // past it -> stop

    px::ParticleEmitter loop;
    loop.mode = px::EmissionMode::Continuous;
    loop.spawnRate = 100.0f;
    loop.duration = 0.1f;
    loop.looping = true;
    CHECK(loop.CalculateSpawnCount(0.05f) == 5);
    CHECK(loop.CalculateSpawnCount(0.10f) > 0); // wraps and keeps emitting
}

TEST_CASE("Prewarm: the effect is already populated on its first Update")
{
    px::ParticleEffect fx(u8"prewarm");
    px::ParticleSystem& sys = fx.AddSystem(500);
    sys.AddInitializer<px::LifetimeInitializer>().lifetime = px::RangeFloat(10.0f, 10.0f);
    sys.emitter.spawnRate = 100.0f;
    sys.prewarmTime = 1.0f; // ~100 particles simulated before the first visible frame

    px::ParticleEffectInstance inst(fx);
    inst.Update(0.016f);
    CHECK(sys.AliveCount() > 50);
}

TEST_CASE("CollisionBehavior: a particle bounces off the ground plane")
{
    px::ParticleEffect fx(u8"collide");
    px::ParticleSystem& sys = fx.AddSystem(10);
    sys.AddInitializer<px::LifetimeInitializer>().lifetime = px::RangeFloat(10.0f, 10.0f);
    px::CollisionBehavior& col = sys.AddBehavior<px::CollisionBehavior>();
    col.planes[0] = px::CollisionPlane{Float3{0.0f, 1.0f, 0.0f}, 0.0f}; // ground y=0
    col.bounce = 0.5f;
    col.friction = 0.0f;
    sys.emitter.mode = px::EmissionMode::Burst;
    sys.emitter.burstCount = 1;

    px::ParticleEffectInstance inst(fx);
    inst.Update(0.016f);
    REQUIRE(sys.AliveCount() == 1);
    // Drive it below the plane moving downward, then step: expect a push-out + upward bounce.
    (*sys.Streams().Positions())[0] = Float3{0.0f, -1.0f, 0.0f};
    (*sys.Streams().Velocities())[0] = Float3{0.0f, -2.0f, 0.0f};
    inst.Update(0.016f);
    CHECK((*sys.Streams().Velocities())[0].y == doctest::Approx(1.0f)); // -(-2)*0.5
    CHECK((*sys.Streams().Positions())[0].y >= -0.001f); // pushed to/above the surface
}

TEST_CASE("Seeded RNG: same seed reproduces spawns; Reset replays deterministically")
{
    auto build = [](px::ParticleSystem& s)
    {
        s.AddInitializer<px::PositionInitializer>().shape = px::EmissionShape::Sphere(3.0f);
        s.AddInitializer<px::LifetimeInitializer>().lifetime = px::RangeFloat(5.0f, 5.0f);
        s.emitter.mode = px::EmissionMode::Burst;
        s.emitter.burstCount = 16;
    };
    px::ParticleEffect a(u8"a");
    px::ParticleSystem& sa = a.AddSystem(100, 12345ull);
    build(sa);
    px::ParticleEffect b(u8"b");
    px::ParticleSystem& sb = b.AddSystem(100, 12345ull);
    build(sb);
    px::ParticleEffectInstance ia(a), ib(b);
    ia.Update(0.016f);
    ib.Update(0.016f);
    REQUIRE(sa.AliveCount() == 16);
    REQUIRE(sb.AliveCount() == 16);
    CHECK(Length((*sa.Streams().Positions())[7] - (*sb.Streams().Positions())[7]) ==
          doctest::Approx(0.0f));

    const Float3 first = (*sa.Streams().Positions())[3];
    sa.Reset();
    ia.Update(0.016f);
    CHECK(Length((*sa.Streams().Positions())[3] - first) == doctest::Approx(0.0f));
}

TEST_CASE("Sub-emitter SpawnAt: inherits (adds) velocity and modulates color")
{
    px::ParticleEffect fx(u8"inherit");
    px::ParticleSystem& child = fx.AddSystem(100);
    child.AddInitializer<px::VelocityInitializer>().baseVelocity = Float3{1.0f, 0.0f, 0.0f};
    child.AddInitializer<px::ColorInitializer>().color =
        px::RangeColor::Constant(Float4{1, 1, 1, 1});
    child.AddInitializer<px::LifetimeInitializer>().lifetime = px::RangeFloat(5.0f, 5.0f);

    child.SpawnAt(1, Float3{0, 0, 0}, Float3{0.0f, 5.0f, 0.0f}, Float4{1.0f, 0.0f, 0.0f, 1.0f});
    REQUIRE(child.AliveCount() == 1);
    const Float3 v = (*child.Streams().Velocities())[0];
    CHECK(v.x == doctest::Approx(1.0f)); // base
    CHECK(v.y == doctest::Approx(5.0f)); // + inherited
    const Float4 c = (*child.Streams().Colors())[0];
    CHECK(c.x == doctest::Approx(1.0f));
    CHECK(c.y == doctest::Approx(0.0f)); // white * red
    CHECK(c.z == doctest::Approx(0.0f));
}

TEST_CASE("CollisionBehavior: sphere + box obstacles push out and reflect")
{
    px::ParticleEffect fx(u8"obstacles");
    px::ParticleSystem& sys = fx.AddSystem(10);
    sys.AddInitializer<px::LifetimeInitializer>().lifetime = px::RangeFloat(10.0f, 10.0f);
    px::CollisionBehavior& col = sys.AddBehavior<px::CollisionBehavior>();
    col.planeCount = 0;
    col.spheres[0] = px::CollisionSphere{Float3{0, 0, 0}, 1.0f};
    col.sphereCount = 1;
    col.boxes[0] = px::CollisionBox{Float3{5, 0, 0}, Float3{1, 1, 1}};
    col.boxCount = 1;
    col.bounce = 1.0f;
    col.friction = 0.0f;
    sys.emitter.mode = px::EmissionMode::Burst;
    sys.emitter.burstCount = 2;

    px::ParticleEffectInstance inst(fx);
    inst.Update(0.016f);
    REQUIRE(sys.AliveCount() == 2);

    // Particle 0: inside the sphere moving toward its centre -> pushed to the surface, velocity reversed.
    (*sys.Streams().Positions())[0] = Float3{0.5f, 0.0f, 0.0f};
    (*sys.Streams().Velocities())[0] = Float3{-1.0f, 0.0f, 0.0f}; // heading inward (toward -x)
    // Particle 1: just inside the box's top face, falling -> popped up to the top, velocity reversed to +y.
    (*sys.Streams().Positions())[1] =
        Float3{5.0f, 0.5f, 0.0f}; // box y-span [-1,1]; 0.5 nearest the +y face
    (*sys.Streams().Velocities())[1] = Float3{0.0f, -1.0f, 0.0f};
    inst.Update(0.016f);

    CHECK((*sys.Streams().Positions())[0].x >= 1.0f - 0.01f); // out to the sphere surface (r=1)
    CHECK((*sys.Streams().Velocities())[0].x == doctest::Approx(1.0f)); // reversed (bounce=1)
    CHECK((*sys.Streams().Positions())[1].y >= 1.0f - 0.01f); // popped up to the box top (y=1)
    CHECK((*sys.Streams().Velocities())[1].y ==
          doctest::Approx(1.0f)); // reflected off the top face
}

TEST_CASE("AlphaOverLifetime sets the envelope (no per-frame accumulation)")
{
    px::ParticleStreamContainer streams(8);
    streams.EnsureStream(px::ParticleStreamId::Color, px::StreamElementType::Float4);
    streams.aliveCount = 1;
    (*streams.Colors())[0] = Float4{1, 1, 1, 1};
    (*streams.Ages())[0] = 0.5f;
    (*streams.Lifetimes())[0] = 1.0f;
    Random rng(1);
    px::ParticleUpdateContext ctx{0.0f, 0.016f, Float3::Zero, &rng};
    px::AlphaOverLifetimeBehavior a;
    a.curve = px::ParticleCurveFloat::Linear(1.0f, 0.0f); // curve(0.5) = 0.5

    a.Update(streams, ctx);
    const f32 first = (*streams.Colors())[0].w;
    a.Update(streams, ctx); // same t across frames must NOT keep multiplying (the old *= bug -> 0)
    a.Update(streams, ctx);
    CHECK(first == doctest::Approx(0.5f));
    CHECK((*streams.Colors())[0].w == doctest::Approx(first)); // stable, not 0.5^3
}
