// Tests for the XML serialization backend: round-trips through Core's
// Serialize() driver in both directions, plus a look at the emitted XML.
#include <doctest/doctest.h>
#include "Core/Prelude.h"
import draconic.core;
import draconic.xml;
import draconic.xml.serialization;
using namespace draconic::core;
using namespace draconic::xml;

namespace
{
    bool Contains(StringView h, StringView n)
    {
        if (n.Size() > h.Size()) { return false; }
        for (usize i = 0; i + n.Size() <= h.Size(); ++i)
        {
            bool m = true;
            for (usize j = 0; j < n.Size(); ++j) { if (h[i + j] != n[j]) { m = false; break; } }
            if (m) { return true; }
        }
        return false;
    }
}

TEST_CASE("xml.serialize: scalar + string round-trip")
{
    String out;
    {
        XmlSerializer w;
        i32 a = -42; u32 b = 7u; f32 c = 1.5f; bool d = true;
        String s = u8"hello world";
        Serialize(w, "a", a);
        Serialize(w, "b", b);
        Serialize(w, "c", c);
        Serialize(w, "d", d);
        Serialize(w, "s", s);
        w.GetOutput(out);
        CHECK(w.IsOk());
    }

    CHECK(Contains(out, u8"name=\"a\""));
    CHECK(Contains(out, u8"<i32 name=\"a\">-42</i32>"));
    CHECK(Contains(out, u8"<string name=\"s\">hello world</string>"));

    XmlDocument doc;
    REQUIRE(doc.Parse(out) == XmlResult::Ok);
    XmlSerializer r(doc);
    i32 a = 0; u32 b = 0; f32 c = 0; bool d = false; String s;
    Serialize(r, "a", a);
    Serialize(r, "b", b);
    Serialize(r, "c", c);
    Serialize(r, "d", d);
    Serialize(r, "s", s);
    CHECK(r.IsOk());
    CHECK(a == -42);
    CHECK(b == 7u);
    CHECK(c == 1.5f);
    CHECK(d == true);
    CHECK(s == StringView(u8"hello world"));
}

TEST_CASE("xml.serialize: nested object (Vec3) round-trip")
{
    String out;
    {
        XmlSerializer w;
        Vec3 v{ 1.0f, 2.5f, -3.0f };
        Serialize(w, "pos", v);
        w.GetOutput(out);
    }
    CHECK(Contains(out, u8"<object name=\"pos\">"));

    XmlDocument doc;
    REQUIRE(doc.Parse(out) == XmlResult::Ok);
    XmlSerializer r(doc);
    Vec3 v{};
    Serialize(r, "pos", v);
    CHECK(r.IsOk());
    CHECK(v.x == 1.0f);
    CHECK(v.y == 2.5f);
    CHECK(v.z == -3.0f);
}

TEST_CASE("xml.serialize: dynamic array round-trip")
{
    String out;
    {
        XmlSerializer w;
        Array<i32> nums;
        nums.PushBack(10); nums.PushBack(20); nums.PushBack(30);
        Serialize(w, "nums", nums);
        w.GetOutput(out);
    }
    CHECK(Contains(out, u8"<array name=\"nums\" count=\"3\">"));

    XmlDocument doc;
    REQUIRE(doc.Parse(out) == XmlResult::Ok);
    XmlSerializer r(doc);
    Array<i32> nums;
    Serialize(r, "nums", nums);
    CHECK(r.IsOk());
    REQUIRE(nums.Size() == 3u);
    CHECK(nums[0] == 10);
    CHECK(nums[1] == 20);
    CHECK(nums[2] == 30);
}

TEST_CASE("xml.serialize: array of objects round-trip")
{
    String out;
    {
        XmlSerializer w;
        Array<Vec2> pts;
        pts.PushBack(Vec2{ 1.0f, 2.0f });
        pts.PushBack(Vec2{ 3.0f, 4.0f });
        Serialize(w, "pts", pts);
        w.GetOutput(out);
    }

    XmlDocument doc;
    REQUIRE(doc.Parse(out) == XmlResult::Ok);
    XmlSerializer r(doc);
    Array<Vec2> pts;
    Serialize(r, "pts", pts);
    CHECK(r.IsOk());
    REQUIRE(pts.Size() == 2u);
    CHECK(pts[0].x == 1.0f); CHECK(pts[0].y == 2.0f);
    CHECK(pts[1].x == 3.0f); CHECK(pts[1].y == 4.0f);
}

TEST_CASE("xml.serialize: Mat4 (positional float array) round-trip")
{
    Mat4 m = Mat4::Identity();
    m.Data()[3] = 9.0f; // tweak one element
    String out;
    {
        XmlSerializer w;
        Serialize(w, "xform", m);
        w.GetOutput(out);
    }

    XmlDocument doc;
    REQUIRE(doc.Parse(out) == XmlResult::Ok);
    XmlSerializer r(doc);
    Mat4 m2{};
    Serialize(r, "xform", m2);
    CHECK(r.IsOk());
    for (int i = 0; i < 16; ++i) { CHECK(m2.Data()[i] == m.Data()[i]); }
}

TEST_CASE("xml.serialize: missing field reports error on read")
{
    XmlDocument doc;
    REQUIRE(doc.Parse(u8"<root><i32 name=\"a\">1</i32></root>") == XmlResult::Ok);
    XmlSerializer r(doc);
    i32 missing = 0;
    Serialize(r, "nope", missing);
    CHECK_FALSE(r.IsOk());
    CHECK(r.GetStatus().Code() == ErrorCode::NotFound);
}
