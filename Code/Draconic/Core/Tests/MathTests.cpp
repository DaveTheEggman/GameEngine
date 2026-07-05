#include <doctest/doctest.h>

#include <cstring>

#include "Core/Debug/Assert.h"
#include "Core/Log/Log.h"
#include "Core/Reflection/Reflect.h"

import draconic.core;

using namespace draconic::core;

// --- Math: scalars ---------------------------------------------------------

TEST_CASE("math: scalar helpers")
{
    CHECK(Abs(-3.0f) == 3.0f);
    CHECK(NearlyEqual(DegreesToRadians(180.0f), kPi));
    CHECK(NearlyEqual(RadiansToDegrees(kPi), 180.0f));
    CHECK(NearlyEqual(Lerp(0.0f, 10.0f, 0.25f), 2.5f));
    CHECK(NearlyEqual(Sqrt(16.0f), 4.0f));
    CHECK(NearlyZero(1.0e-8f));

    static_assert(Abs(-1.0f) == 1.0f);
}

// --- Math: Vector3 ------------------------------------------------------------

TEST_CASE("math: Vector3 arithmetic")
{
    Vector3 a{ 1.0f, 2.0f, 3.0f };
    Vector3 b{ 4.0f, 5.0f, 6.0f };

    CHECK((a + b) == Vector3{ 5.0f, 7.0f, 9.0f });
    CHECK((b - a) == Vector3{ 3.0f, 3.0f, 3.0f });
    CHECK((a * 2.0f) == Vector3{ 2.0f, 4.0f, 6.0f });
    CHECK((2.0f * a) == Vector3{ 2.0f, 4.0f, 6.0f });
    CHECK((-a) == Vector3{ -1.0f, -2.0f, -3.0f });

    a += b;
    CHECK(a == Vector3{ 5.0f, 7.0f, 9.0f });

    CHECK(a[0] == 5.0f);
    CHECK(a[2] == 9.0f);

    static_assert(Vector3{ 1.0f, 0.0f, 0.0f } == Vector3::UnitX);
}

TEST_CASE("math: Vector3 dot, cross, length, normalize")
{
    CHECK(Dot(Vector3{ 1.0f, 2.0f, 3.0f }, Vector3{ 4.0f, 5.0f, 6.0f }) == 32.0f);

    // Right-handed cross: X x Y = Z
    CHECK(Cross(Vector3::UnitX, Vector3::UnitY) == Vector3::UnitZ);
    CHECK(Cross(Vector3::UnitY, Vector3::UnitZ) == Vector3::UnitX);

    CHECK(LengthSquared(Vector3{ 3.0f, 4.0f, 0.0f }) == 25.0f);
    CHECK(NearlyEqual(Length(Vector3{ 3.0f, 4.0f, 0.0f }), 5.0f));

    Vector3 n = Normalized(Vector3{ 0.0f, 8.0f, 0.0f });
    CHECK(NearlyEqual(n, Vector3::UnitY));
    CHECK(NearlyEqual(Length(n), 1.0f));

    // Degenerate input -> Zero, no NaN/divide-by-zero.
    CHECK(Normalized(Vector3::Zero) == Vector3::Zero);
}

TEST_CASE("math: Vector3 lerp/min/max")
{
    CHECK(Lerp(Vector3::Zero, Vector3{ 4.0f, 8.0f, 12.0f }, 0.5f) == Vector3{ 2.0f, 4.0f, 6.0f });
    CHECK(Min(Vector3{ 1.0f, 5.0f, 3.0f }, Vector3{ 4.0f, 2.0f, 6.0f }) == Vector3{ 1.0f, 2.0f, 3.0f });
    CHECK(Max(Vector3{ 1.0f, 5.0f, 3.0f }, Vector3{ 4.0f, 2.0f, 6.0f }) == Vector3{ 4.0f, 5.0f, 6.0f });
}

// --- Math: Vector2 / Vector4 -----------------------------------------------------

TEST_CASE("math: Vector2 and Vector4 basics")
{
    CHECK(Dot(Vector2{ 1.0f, 2.0f }, Vector2{ 3.0f, 4.0f }) == 11.0f);
    CHECK(NearlyEqual(Length(Vector2{ 3.0f, 4.0f }), 5.0f));

    Vector4 v{ Vector3{ 1.0f, 2.0f, 3.0f }, 1.0f };
    CHECK(v.XYZ() == Vector3{ 1.0f, 2.0f, 3.0f });
    CHECK(v.w == 1.0f);
    CHECK(Dot(Vector4::One, Vector4::One) == 4.0f);
}

// --- Math: Matrix4 ------------------------------------------------------------

TEST_CASE("math: Matrix4 identity and multiply")
{
    const Matrix4 id = Matrix4::Identity();
    const Matrix4 t = Matrix4::Translation(Vector3{ 1.0f, 2.0f, 3.0f });

    CHECK(NearlyEqual(id * t, t));
    CHECK(NearlyEqual(t * id, t));

    static_assert(Matrix4::Identity()(0, 0) == 1.0f);
    static_assert(Matrix4::Identity()(0, 1) == 0.0f);
}

TEST_CASE("math: Matrix4 translation lives in the last row (row vectors)")
{
    const Matrix4 t = Matrix4::Translation(Vector3{ 10.0f, 20.0f, 30.0f });
    CHECK(t.m[3][0] == 10.0f);
    CHECK(t.m[3][1] == 20.0f);
    CHECK(t.m[3][2] == 30.0f);

    const Vector3 p = TransformPoint(Vector3{ 1.0f, 1.0f, 1.0f }, t);
    CHECK(NearlyEqual(p, Vector3{ 11.0f, 21.0f, 31.0f }));

    // Directions ignore translation.
    CHECK(NearlyEqual(TransformDirection(Vector3{ 1.0f, 0.0f, 0.0f }, t), Vector3{ 1.0f, 0.0f, 0.0f }));
}

TEST_CASE("math: Matrix4 rotation (row vectors): RotationZ(90) maps +X to +Y")
{
    const Matrix4 rz = Matrix4::RotationZ(DegreesToRadians(90.0f));
    CHECK(NearlyEqual(TransformDirection(Vector3::UnitX, rz), Vector3::UnitY));

    const Matrix4 ry = Matrix4::RotationY(DegreesToRadians(90.0f));
    // RotationY(90) maps +Z to +X.
    CHECK(NearlyEqual(TransformDirection(Vector3::UnitZ, ry), Vector3::UnitX));
}

TEST_CASE("math: Matrix4 2D affine helpers (TransformPoint2D, operator==)")
{
    CHECK(Matrix4::Identity() == Matrix4::Identity());
    CHECK_FALSE(Matrix4::Translation(Vector3{ 1, 0, 0 }) == Matrix4::Identity());

    // 2D translate.
    const Matrix4 t = Matrix4::Translation(Vector3{ 5.0f, 7.0f, 0.0f });
    CHECK(NearlyEqual(TransformPoint2D(Vector2{ 1.0f, 2.0f }, t), Vector2{ 6.0f, 9.0f }));

    // 2D scale-then-translate (row vectors, left-to-right): v * (S * T).
    const Matrix4 st = Matrix4::Scale(Vector3{ 2.0f, 3.0f, 1.0f }) * Matrix4::Translation(Vector3{ 1.0f, 1.0f, 0.0f });
    CHECK(NearlyEqual(TransformPoint2D(Vector2{ 1.0f, 1.0f }, st), Vector2{ 3.0f, 4.0f })); // (2,3)+(1,1)

    // RotationZ(90) maps +X to +Y in 2D too.
    const Matrix4 rz = Matrix4::RotationZ(DegreesToRadians(90.0f));
    CHECK(NearlyEqual(TransformPoint2D(Vector2{ 1.0f, 0.0f }, rz), Vector2{ 0.0f, 1.0f }));
}

TEST_CASE("math: Matrix4 composition reads left-to-right (scale then translate)")
{
    // v * (S * T): scale first, then translate.
    const Matrix4 st = Matrix4::Scale(Vector3{ 2.0f, 2.0f, 2.0f }) * Matrix4::Translation(Vector3{ 1.0f, 0.0f, 0.0f });
    const Vector3 p = TransformPoint(Vector3{ 1.0f, 1.0f, 1.0f }, st);
    CHECK(NearlyEqual(p, Vector3{ 3.0f, 2.0f, 2.0f })); // (2,2,2) + (1,0,0)
}

TEST_CASE("math: perspective has the expected projective structure")
{
    const Matrix4 proj = Matrix4::PerspectiveFovRH(DegreesToRadians(90.0f), 1.0f, 1.0f, 100.0f);
    CHECK(proj.m[2][3] == -1.0f);              // w' = -z (RH)
    CHECK(NearlyEqual(proj.m[0][0], 1.0f));    // xScale = 1/tan(45) at aspect 1
}

// --- Math: Quaternion ------------------------------------------------------------

TEST_CASE("math: Quaternion rotates vectors and agrees with its matrix")
{
    const Quaternion q = Quaternion::FromAxisAngle(Vector3::UnitZ, DegreesToRadians(90.0f));

    // 90 deg about Z maps +X to +Y.
    CHECK(NearlyEqual(RotateVector(q, Vector3::UnitX), Vector3::UnitY));

    // Quaternion rotation and its matrix agree.
    const Matrix4 r = RotationMatrix(q);
    CHECK(NearlyEqual(RotateVector(q, Vector3::UnitX), TransformDirection(Vector3::UnitX, r)));
    CHECK(NearlyEqual(RotateVector(q, Vector3{ 0.3f, -0.5f, 0.8f }),
                      TransformDirection(Vector3{ 0.3f, -0.5f, 0.8f }, r)));

    // Identity does nothing.
    CHECK(NearlyEqual(RotateVector(Quaternion::Identity, Vector3{ 1.0f, 2.0f, 3.0f }), Vector3{ 1.0f, 2.0f, 3.0f }));

    // Composition: two 45-deg rotations == one 90-deg.
    const Quaternion half = Quaternion::FromAxisAngle(Vector3::UnitZ, DegreesToRadians(45.0f));
    CHECK(NearlyEqual(RotateVector(half * half, Vector3::UnitX), Vector3::UnitY));
}

// --- Math: Transform -------------------------------------------------------

TEST_CASE("math: Transform composes scale, rotation, translation")
{
    Transform xform;
    xform.scale = Vector3{ 2.0f, 2.0f, 2.0f };
    xform.rotation = Quaternion::FromAxisAngle(Vector3::UnitZ, DegreesToRadians(90.0f));
    xform.position = Vector3{ 5.0f, 0.0f, 0.0f };

    const Matrix4 m = xform.ToMatrix();

    // (1,0,0) -> scale*2 -> (2,0,0) -> rot90Z -> (0,2,0) -> +translate -> (5,2,0)
    const Vector3 p = TransformPoint(Vector3::UnitX, m);
    CHECK(NearlyEqual(p, Vector3{ 5.0f, 2.0f, 0.0f }));

    // Identity transform is a no-op.
    Transform identity;
    CHECK(NearlyEqual(TransformPoint(Vector3{ 7.0f, 8.0f, 9.0f }, identity.ToMatrix()),
                      Vector3{ 7.0f, 8.0f, 9.0f }));
}

// --- Math: Matrix4 determinant / inverse --------------------------------------

TEST_CASE("math: Matrix4 determinant")
{
    CHECK(NearlyEqual(Determinant(Matrix4::Identity()), 1.0f));
    CHECK(NearlyEqual(Determinant(Matrix4::Scale(Vector3{ 2.0f, 3.0f, 4.0f })), 24.0f));
}

TEST_CASE("math: Matrix4 inverse undoes the transform")
{
    Transform xform;
    xform.scale = Vector3{ 2.0f, 0.5f, 3.0f };
    xform.rotation = Quaternion::FromAxisAngle(Normalized(Vector3{ 1.0f, 2.0f, 3.0f }), DegreesToRadians(50.0f));
    xform.position = Vector3{ 5.0f, -2.0f, 1.0f };

    const Matrix4 m = xform.ToMatrix();
    const Matrix4 inv = Inverse(m);

    CHECK(NearlyEqual(m * inv, Matrix4::Identity(), 1.0e-3f));
    CHECK(NearlyEqual(inv * m, Matrix4::Identity(), 1.0e-3f));

    // A point transformed then inverse-transformed returns to itself.
    const Vector3 p{ 3.0f, 4.0f, 5.0f };
    const Vector3 roundTrip = TransformPoint(TransformPoint(p, m), inv);
    CHECK(NearlyEqual(roundTrip, p, 1.0e-3f));

    // Singular matrix -> Identity (no divide-by-zero).
    CHECK(NearlyEqual(Inverse(Matrix4::Scale(Vector3::Zero)), Matrix4::Identity()));
}

// --- Math: Quaternion Slerp ------------------------------------------------------

TEST_CASE("math: Quaternion Slerp endpoints and midpoint")
{
    const Quaternion a = Quaternion::Identity;
    const Quaternion b = Quaternion::FromAxisAngle(Vector3::UnitZ, DegreesToRadians(90.0f));

    CHECK(NearlyEqual(Slerp(a, b, 0.0f), a));
    CHECK(NearlyEqual(Slerp(a, b, 1.0f), b));

    // Halfway between 0 and 90 deg about Z is 45 deg: maps +X to (cos45, sin45, 0).
    const Quaternion mid = Slerp(a, b, 0.5f);
    const Vector3 rotated = RotateVector(mid, Vector3::UnitX);
    const f32 c = Cos(DegreesToRadians(45.0f));
    CHECK(NearlyEqual(rotated, Vector3{ c, c, 0.0f }, 1.0e-4f));
}

// --- Math: Vector2 / Vector4 normalize -------------------------------------------

TEST_CASE("math: Vector2 and Vector4 Normalized")
{
    CHECK(NearlyEqual(Length(Normalized(Vector2{ 3.0f, 4.0f })), 1.0f));
    CHECK(Normalized(Vector2::Zero) == Vector2::Zero);

    CHECK(NearlyEqual(Length(Normalized(Vector4{ 1.0f, 2.0f, 2.0f, 4.0f })), 1.0f));
    CHECK(Normalized(Vector4::Zero) == Vector4::Zero);
}

// --- Math: Matrix3 ------------------------------------------------------------

TEST_CASE("math: Matrix3 identity, multiply, transpose")
{
    const Matrix3 id = Matrix3::Identity();
    const Matrix3 r = Matrix3::FromMat4(Matrix4::RotationZ(DegreesToRadians(90.0f)));

    CHECK(NearlyEqual(id * r, r));
    CHECK(NearlyEqual(Transpose(Transpose(r)), r));

    static_assert(Matrix3::Identity()(1, 1) == 1.0f);
}

TEST_CASE("math: Matrix3 row-vector rotation matches Matrix4")
{
    const Matrix3 rz = Matrix3::FromMat4(Matrix4::RotationZ(DegreesToRadians(90.0f)));
    CHECK(NearlyEqual(Vector3::UnitX * rz, Vector3::UnitY));
}

TEST_CASE("math: Matrix3 determinant and inverse")
{
    const Matrix3 rz = Matrix3::FromMat4(Matrix4::RotationZ(DegreesToRadians(37.0f)));
    CHECK(NearlyEqual(Determinant(rz), 1.0f)); // pure rotation

    const Matrix3 inv = Inverse(rz);
    CHECK(NearlyEqual(rz * inv, Matrix3::Identity(), 1.0e-4f));

    // For a rotation, the inverse equals the transpose.
    CHECK(NearlyEqual(inv, Transpose(rz), 1.0e-4f));

    // Singular -> Identity.
    Matrix3 zero{};
    CHECK(NearlyEqual(Inverse(zero), Matrix3::Identity()));
}

// --- Math: geometry --------------------------------------------------------

TEST_CASE("geometry: AABB contains, expand, merge")
{
    AABB box{ Vector3{ 0.0f, 0.0f, 0.0f }, Vector3{ 2.0f, 2.0f, 2.0f } };
    CHECK(box.Contains(Vector3{ 1.0f, 1.0f, 1.0f }));
    CHECK_FALSE(box.Contains(Vector3{ 3.0f, 1.0f, 1.0f }));
    CHECK(NearlyEqual(box.Center(), Vector3{ 1.0f, 1.0f, 1.0f }));
    CHECK(NearlyEqual(box.Extents(), Vector3{ 1.0f, 1.0f, 1.0f }));

    // Build from points via Empty + Expand.
    AABB grown = AABB::Empty();
    CHECK_FALSE(grown.IsValid());
    grown.Expand(Vector3{ -1.0f, 0.0f, 5.0f });
    grown.Expand(Vector3{ 3.0f, 4.0f, -2.0f });
    CHECK(grown.IsValid());
    CHECK(NearlyEqual(grown.min, Vector3{ -1.0f, 0.0f, -2.0f }));
    CHECK(NearlyEqual(grown.max, Vector3{ 3.0f, 4.0f, 5.0f }));

    AABB a{ Vector3{ 0.0f, 0.0f, 0.0f }, Vector3{ 1.0f, 1.0f, 1.0f } };
    AABB b{ Vector3{ 2.0f, 2.0f, 2.0f }, Vector3{ 3.0f, 3.0f, 3.0f } };
    CHECK_FALSE(a.Intersects(b));
    AABB m = Merge(a, b);
    CHECK(NearlyEqual(m.min, Vector3::Zero));
    CHECK(NearlyEqual(m.max, Vector3{ 3.0f, 3.0f, 3.0f }));
    CHECK(m.Intersects(a));
}

TEST_CASE("geometry: Plane signed distance")
{
    // XZ plane at y = 0, normal +Y.
    const Plane plane = Plane::FromPointNormal(Vector3::Zero, Vector3::UnitY);
    CHECK(NearlyEqual(plane.SignedDistance(Vector3{ 5.0f, 0.0f, -3.0f }), 0.0f));
    CHECK(NearlyEqual(plane.SignedDistance(Vector3{ 0.0f, 2.0f, 0.0f }), 2.0f));
    CHECK(NearlyEqual(plane.SignedDistance(Vector3{ 0.0f, -4.0f, 0.0f }), -4.0f));

    const Plane unnormalized{ Vector3{ 0.0f, 3.0f, 0.0f }, 0.0f };
    CHECK(NearlyEqual(Length(unnormalized.Normalized().normal), 1.0f));
}

TEST_CASE("geometry: Rectangle contains and intersects")
{
    Rectangle r{ 0.0f, 0.0f, 4.0f, 2.0f };
    CHECK(r.Contains(Vector2{ 2.0f, 1.0f }));
    CHECK_FALSE(r.Contains(Vector2{ 5.0f, 1.0f }));
    CHECK(NearlyEqual(r.Center(), Vector2{ 2.0f, 1.0f }));

    CHECK(r.Intersects(Rectangle{ 3.0f, 1.0f, 2.0f, 2.0f }));
    CHECK_FALSE(r.Intersects(Rectangle{ 10.0f, 10.0f, 1.0f, 1.0f }));
}

// --- Math: Color -----------------------------------------------------------

TEST_CASE("color: pack/unpack and operations")
{
    CHECK(Color::White.ToRGBA8() == 0xFFFFFFFFu);
    CHECK(Color::Red.ToRGBA8() == 0xFF0000FFu);
    CHECK(Color::Transparent.ToRGBA8() == 0x00000000u);

    const Color c = Color::FromRGBA8(0xFF0000FFu);
    CHECK(NearlyEqual(c, Color::Red));

    CHECK(NearlyEqual(Lerp(Color::Black, Color::White, 0.5f), Color{ 0.5f, 0.5f, 0.5f, 1.0f }));
    CHECK(NearlyEqual(Color::White * 0.25f, Color{ 0.25f, 0.25f, 0.25f, 0.25f }));

    // Round-trip through 8-bit packing (within quantization tolerance).
    const Color original{ 0.2f, 0.4f, 0.6f, 0.8f };
    CHECK(NearlyEqual(Color::FromRGBA8(original.ToRGBA8()), original, 1.0f / 255.0f));
}

TEST_CASE("color32: packed byte color and conversions")
{
    CHECK(Color32::White.ToRGBA8() == 0xFFFFFFFFu);
    CHECK(Color32::Red.ToRGBA8() == 0xFF0000FFu);
    CHECK(Color32::Transparent.ToRGBA8() == 0x00000000u);
    CHECK(Color32::FromRGBA8(0x10203040u) == Color32{ 0x10, 0x20, 0x30, 0x40 });

    // Float <-> byte conversions, no gamma.
    CHECK(ToColor32(Color::Red) == Color32::Red);
    CHECK(NearlyEqual(ToColor(Color32::Blue), Color::Blue));

    // Every byte value round-trips exactly: Color32 -> Color -> Color32.
    bool exact = true;
    for (u32 v = 0; v <= 255; ++v)
    {
        const Color32 c{ static_cast<u8>(v), static_cast<u8>(255 - v), static_cast<u8>(v), 255 };
        if (!(ToColor32(ToColor(c)) == c)) { exact = false; break; }
    }
    CHECK(exact);
}

// --- Math: easing functions (ported from Sedulous.Core.Mathematics.Easings) ---

TEST_CASE("math: easing endpoints + known values")
{
    // Every easing maps 0 -> ~0 and 1 -> ~1 (the interpolation factor endpoints).
    EasingFunction fns[] = {
        EaseInLinear, EaseOutLinear,
        EaseInQuadratic, EaseOutQuadratic, EaseInOutQuadratic,
        EaseInCubic, EaseOutCubic, EaseInOutCubic,
        EaseInQuartic, EaseOutQuartic, EaseInOutQuartic,
        EaseInQuintic, EaseOutQuintic, EaseInOutQuintic,
        EaseInSin, EaseOutSin, EaseInOutSin,
        EaseInExponential, EaseOutExponential, EaseInOutExponential,
        EaseInCircular, EaseOutCircular, EaseInOutCircular,
        EaseInBack, EaseOutBack, EaseInOutBack,
        EaseInElastic, EaseOutElastic, EaseInOutElastic,
        EaseInBounce, EaseOutBounce, EaseInOutBounce,
    };
    for (EasingFunction f : fns)
    {
        CHECK(NearlyEqual(f(0.0f), 0.0f));
        CHECK(NearlyEqual(f(1.0f), 1.0f));
    }

    // Specific known polynomial values.
    CHECK(NearlyEqual(EaseInQuadratic(0.5f), 0.25f));
    CHECK(NearlyEqual(EaseOutQuadratic(0.5f), 0.75f));
    CHECK(NearlyEqual(EaseInOutQuadratic(0.5f), 0.5f));
    CHECK(NearlyEqual(EaseInCubic(0.5f), 0.125f));
    CHECK(NearlyEqual(EaseInOutCubic(0.5f), 0.5f));
    CHECK(NearlyEqual(EaseInOutSin(0.5f), 0.5f));
    // Symmetric in/out-out: in/out midpoints land on 0.5 for odd-symmetric families.
    CHECK(NearlyEqual(EaseInOutQuartic(0.5f), 0.5f));
    CHECK(NearlyEqual(EaseInOutQuintic(0.5f), 0.5f));
    CHECK(NearlyEqual(EaseInOutCircular(0.5f), 0.5f));
}

// --- Math: Transform Lerp (BoneTransform.Lerp equivalent) ---

TEST_CASE("math: Transform Lerp + identity")
{
    CHECK(NearlyEqual(IdentityTransform.position, Vector3::Zero));
    CHECK(NearlyEqual(IdentityTransform.scale, Vector3::One));

    Transform a{ Vector3{ 0, 0, 0 }, Quaternion::Identity, Vector3{ 1, 1, 1 } };
    Transform b{ Vector3{ 2, 4, 6 }, Quaternion::Identity, Vector3{ 3, 3, 3 } };
    Transform m = Transform::Lerp(a, b, 0.5f);
    CHECK(NearlyEqual(m.position, Vector3{ 1, 2, 3 }));
    CHECK(NearlyEqual(m.scale, Vector3{ 2, 2, 2 }));

    // Endpoints return the inputs.
    Transform at0 = Transform::Lerp(a, b, 0.0f);
    Transform at1 = Transform::Lerp(a, b, 1.0f);
    CHECK(NearlyEqual(at0.position, a.position));
    CHECK(NearlyEqual(at1.position, b.position));
}
