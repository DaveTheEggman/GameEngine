// draconic.net.replication - NetworkId, the replicated-field layout harvest, and the reflection-
// driven Variant<->wire codec. The central bet: a component marks fields Replicated and the wire
// format is GENERATED from reflection - no hand-written per-component net code.
#include <doctest/doctest.h>
#include "Core/Prelude.h"
#include "Core/Reflection/Reflect.h"

import draconic.core;
import draconic.net;              // BitWriter / BitReader
import draconic.net.replication;

using namespace draconic::core;
namespace net = draconic::net;

namespace
{
    // A stand-in networked component: a transform-like mix of replicated + local-only fields, plus a
    // marked-but-unsupported field (String) to exercise layout exclusion.
    struct Mover
    {
        Float3 position{ 0.0f, 0.0f, 0.0f };      // replicated
        Quaternion rotation{ 0.0f, 0.0f, 0.0f, 1.0f };  // replicated
        f32 speed = 0.0f;                          // replicated
        bool grounded = false;                     // replicated
        i32 health = 0;                            // replicated
        f32 localOnly = 0.0f;                      // NOT replicated (no marker)
        String label;                              // marked replicated but unsupported type -> excluded
    };
}

DRACONIC_REFLECT_VALUE(Mover, "draconic::net::test")
{
    builder.Property<&Mover::position>("position").PropAttribute(net::kReplicatedAttribute, true);
    builder.Property<&Mover::rotation>("rotation").PropAttribute(net::kReplicatedAttribute, true);
    builder.Property<&Mover::speed>("speed").PropAttribute(net::kReplicatedAttribute, true);
    builder.Property<&Mover::grounded>("grounded").PropAttribute(net::kReplicatedAttribute, true);
    builder.Property<&Mover::health>("health").PropAttribute(net::kReplicatedAttribute, true);
    builder.Property<&Mover::localOnly>("localOnly");   // no marker -> local
    builder.Property<&Mover::label>("label").PropAttribute(net::kReplicatedAttribute, true);  // unsupported
}

TEST_CASE("replication: NetworkId validity + equality")
{
    CHECK_FALSE(net::NetworkId::Invalid().IsValid());
    CHECK(net::NetworkId{ 7 }.IsValid());
    CHECK(net::NetworkId{ 7 } == net::NetworkId{ 7 });
    CHECK(net::NetworkId{ 7 } != net::NetworkId{ 8 });
}

TEST_CASE("replication: layout harvest picks marked + supported fields only")
{
    DraconicRegisterValue_Mover();
    const Span<const PropertyInfo* const> layout = net::ReplicatedProperties(TypeOf<Mover>());
    // position, rotation, speed, grounded, health = 5. localOnly (unmarked) + label (unsupported) out.
    REQUIRE(layout.Size() == 5u);
    CHECK(StringView(reinterpret_cast<const char8_t*>(layout[0]->name)) == u8"position");
    CHECK(StringView(reinterpret_cast<const char8_t*>(layout[4]->name)) == u8"health");
}

TEST_CASE("replication: a component round-trips its replicated fields through the wire")
{
    DraconicRegisterValue_Mover();

    Mover source;
    source.position = Float3{ 1.5f, -2.0f, 3.25f };
    source.rotation = Quaternion{ 0.1f, 0.2f, 0.3f, 0.9f };
    source.speed = 12.5f;
    source.grounded = true;
    source.health = 77;
    source.localOnly = 999.0f;   // must NOT cross the wire
    source.label = String(u8"ignored");

    net::BitWriter writer;
    const usize wrote = net::WriteReplicatedState(writer, Instance::From(&source));
    CHECK(wrote == 5u);

    Mover dest;                  // all defaults
    net::BitReader reader(writer.Data());
    const usize read = net::ReadReplicatedState(reader, Instance::From(&dest));
    CHECK(read == 5u);
    CHECK(reader.Ok());

    CHECK(dest.position == source.position);
    CHECK(dest.rotation.x == doctest::Approx(0.1f));
    CHECK(dest.rotation.w == doctest::Approx(0.9f));
    CHECK(dest.speed == doctest::Approx(12.5f));
    CHECK(dest.grounded == true);
    CHECK(dest.health == 77);
    CHECK(dest.localOnly == doctest::Approx(0.0f));   // local field untouched by replication
    CHECK(dest.label.IsEmpty());                       // unsupported field never replicated
}

TEST_CASE("replication: field codec round-trips supported scalars + rejects unsupported")
{
    // f64 (full precision), Float3, i32.
    net::BitWriter writer;
    CHECK(net::WriteFieldValue(writer, Variant::From<f64>(3.141592653589793)));
    CHECK(net::WriteFieldValue(writer, Variant::From<Float3>(Float3{ 4.0f, 5.0f, 6.0f })));
    CHECK(net::WriteFieldValue(writer, Variant::From<i32>(-42)));
    // A String value has no codec support -> false, nothing written.
    CHECK_FALSE(net::WriteFieldValue(writer, Variant::From<String>(String(u8"nope"))));

    net::BitReader reader(writer.Data());
    Variant a, b, c;
    REQUIRE(net::ReadFieldValue(reader, &TypeOf<f64>(), a));
    REQUIRE(net::ReadFieldValue(reader, &TypeOf<Float3>(), b));
    REQUIRE(net::ReadFieldValue(reader, &TypeOf<i32>(), c));
    CHECK(*a.TryGet<f64>() == doctest::Approx(3.141592653589793));
    CHECK(*b.TryGet<Float3>() == Float3{ 4.0f, 5.0f, 6.0f });
    CHECK(*c.TryGet<i32>() == -42);
    // Reading an unsupported type consumes nothing and reports false.
    Variant d;
    CHECK_FALSE(net::ReadFieldValue(reader, &TypeOf<String>(), d));
}
