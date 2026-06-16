#include <doctest/doctest.h>

#include <cstring>

#include "Core/Debug/Assert.h"
#include "Core/Log/Log.h"
#include "Core/RTTI/Reflect.h"

import raptor.core;

using namespace raptor::core;

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

// --- Math: Vec3 ------------------------------------------------------------

TEST_CASE("math: Vec3 arithmetic")
{
    Vec3 a{ 1.0f, 2.0f, 3.0f };
    Vec3 b{ 4.0f, 5.0f, 6.0f };

    CHECK((a + b) == Vec3{ 5.0f, 7.0f, 9.0f });
    CHECK((b - a) == Vec3{ 3.0f, 3.0f, 3.0f });
    CHECK((a * 2.0f) == Vec3{ 2.0f, 4.0f, 6.0f });
    CHECK((2.0f * a) == Vec3{ 2.0f, 4.0f, 6.0f });
    CHECK((-a) == Vec3{ -1.0f, -2.0f, -3.0f });

    a += b;
    CHECK(a == Vec3{ 5.0f, 7.0f, 9.0f });

    CHECK(a[0] == 5.0f);
    CHECK(a[2] == 9.0f);

    static_assert(Vec3{ 1.0f, 0.0f, 0.0f } == Vec3::UnitX);
}

TEST_CASE("math: Vec3 dot, cross, length, normalize")
{
    CHECK(Dot(Vec3{ 1.0f, 2.0f, 3.0f }, Vec3{ 4.0f, 5.0f, 6.0f }) == 32.0f);

    // Right-handed cross: X x Y = Z
    CHECK(Cross(Vec3::UnitX, Vec3::UnitY) == Vec3::UnitZ);
    CHECK(Cross(Vec3::UnitY, Vec3::UnitZ) == Vec3::UnitX);

    CHECK(LengthSquared(Vec3{ 3.0f, 4.0f, 0.0f }) == 25.0f);
    CHECK(NearlyEqual(Length(Vec3{ 3.0f, 4.0f, 0.0f }), 5.0f));

    Vec3 n = Normalized(Vec3{ 0.0f, 8.0f, 0.0f });
    CHECK(NearlyEqual(n, Vec3::UnitY));
    CHECK(NearlyEqual(Length(n), 1.0f));

    // Degenerate input -> Zero, no NaN/divide-by-zero.
    CHECK(Normalized(Vec3::Zero) == Vec3::Zero);
}

TEST_CASE("math: Vec3 lerp/min/max")
{
    CHECK(Lerp(Vec3::Zero, Vec3{ 4.0f, 8.0f, 12.0f }, 0.5f) == Vec3{ 2.0f, 4.0f, 6.0f });
    CHECK(Min(Vec3{ 1.0f, 5.0f, 3.0f }, Vec3{ 4.0f, 2.0f, 6.0f }) == Vec3{ 1.0f, 2.0f, 3.0f });
    CHECK(Max(Vec3{ 1.0f, 5.0f, 3.0f }, Vec3{ 4.0f, 2.0f, 6.0f }) == Vec3{ 4.0f, 5.0f, 6.0f });
}

// --- Math: Vec2 / Vec4 -----------------------------------------------------

TEST_CASE("math: Vec2 and Vec4 basics")
{
    CHECK(Dot(Vec2{ 1.0f, 2.0f }, Vec2{ 3.0f, 4.0f }) == 11.0f);
    CHECK(NearlyEqual(Length(Vec2{ 3.0f, 4.0f }), 5.0f));

    Vec4 v{ Vec3{ 1.0f, 2.0f, 3.0f }, 1.0f };
    CHECK(v.XYZ() == Vec3{ 1.0f, 2.0f, 3.0f });
    CHECK(v.w == 1.0f);
    CHECK(Dot(Vec4::One, Vec4::One) == 4.0f);
}

// --- Math: Mat4 ------------------------------------------------------------

TEST_CASE("math: Mat4 identity and multiply")
{
    const Mat4 id = Mat4::Identity();
    const Mat4 t = Mat4::Translation(Vec3{ 1.0f, 2.0f, 3.0f });

    CHECK(NearlyEqual(id * t, t));
    CHECK(NearlyEqual(t * id, t));

    static_assert(Mat4::Identity()(0, 0) == 1.0f);
    static_assert(Mat4::Identity()(0, 1) == 0.0f);
}

TEST_CASE("math: Mat4 translation lives in the last row (row vectors)")
{
    const Mat4 t = Mat4::Translation(Vec3{ 10.0f, 20.0f, 30.0f });
    CHECK(t.m[3][0] == 10.0f);
    CHECK(t.m[3][1] == 20.0f);
    CHECK(t.m[3][2] == 30.0f);

    const Vec3 p = TransformPoint(Vec3{ 1.0f, 1.0f, 1.0f }, t);
    CHECK(NearlyEqual(p, Vec3{ 11.0f, 21.0f, 31.0f }));

    // Directions ignore translation.
    CHECK(NearlyEqual(TransformDirection(Vec3{ 1.0f, 0.0f, 0.0f }, t), Vec3{ 1.0f, 0.0f, 0.0f }));
}

TEST_CASE("math: Mat4 rotation (row vectors): RotationZ(90) maps +X to +Y")
{
    const Mat4 rz = Mat4::RotationZ(DegreesToRadians(90.0f));
    CHECK(NearlyEqual(TransformDirection(Vec3::UnitX, rz), Vec3::UnitY));

    const Mat4 ry = Mat4::RotationY(DegreesToRadians(90.0f));
    // RotationY(90) maps +Z to +X.
    CHECK(NearlyEqual(TransformDirection(Vec3::UnitZ, ry), Vec3::UnitX));
}

TEST_CASE("math: Mat4 composition reads left-to-right (scale then translate)")
{
    // v * (S * T): scale first, then translate.
    const Mat4 st = Mat4::Scale(Vec3{ 2.0f, 2.0f, 2.0f }) * Mat4::Translation(Vec3{ 1.0f, 0.0f, 0.0f });
    const Vec3 p = TransformPoint(Vec3{ 1.0f, 1.0f, 1.0f }, st);
    CHECK(NearlyEqual(p, Vec3{ 3.0f, 2.0f, 2.0f })); // (2,2,2) + (1,0,0)
}

TEST_CASE("math: perspective has the expected projective structure")
{
    const Mat4 proj = Mat4::PerspectiveFovRH(DegreesToRadians(90.0f), 1.0f, 1.0f, 100.0f);
    CHECK(proj.m[2][3] == -1.0f);              // w' = -z (RH)
    CHECK(NearlyEqual(proj.m[0][0], 1.0f));    // xScale = 1/tan(45) at aspect 1
}

// --- Math: Quat ------------------------------------------------------------

TEST_CASE("math: Quat rotates vectors and agrees with its matrix")
{
    const Quat q = Quat::FromAxisAngle(Vec3::UnitZ, DegreesToRadians(90.0f));

    // 90 deg about Z maps +X to +Y.
    CHECK(NearlyEqual(RotateVector(q, Vec3::UnitX), Vec3::UnitY));

    // Quaternion rotation and its matrix agree.
    const Mat4 r = RotationMatrix(q);
    CHECK(NearlyEqual(RotateVector(q, Vec3::UnitX), TransformDirection(Vec3::UnitX, r)));
    CHECK(NearlyEqual(RotateVector(q, Vec3{ 0.3f, -0.5f, 0.8f }),
                      TransformDirection(Vec3{ 0.3f, -0.5f, 0.8f }, r)));

    // Identity does nothing.
    CHECK(NearlyEqual(RotateVector(Quat::Identity, Vec3{ 1.0f, 2.0f, 3.0f }), Vec3{ 1.0f, 2.0f, 3.0f }));

    // Composition: two 45-deg rotations == one 90-deg.
    const Quat half = Quat::FromAxisAngle(Vec3::UnitZ, DegreesToRadians(45.0f));
    CHECK(NearlyEqual(RotateVector(half * half, Vec3::UnitX), Vec3::UnitY));
}

// --- Math: Transform -------------------------------------------------------

TEST_CASE("math: Transform composes scale, rotation, translation")
{
    Transform xform;
    xform.scale = Vec3{ 2.0f, 2.0f, 2.0f };
    xform.rotation = Quat::FromAxisAngle(Vec3::UnitZ, DegreesToRadians(90.0f));
    xform.position = Vec3{ 5.0f, 0.0f, 0.0f };

    const Mat4 m = xform.ToMatrix();

    // (1,0,0) -> scale*2 -> (2,0,0) -> rot90Z -> (0,2,0) -> +translate -> (5,2,0)
    const Vec3 p = TransformPoint(Vec3::UnitX, m);
    CHECK(NearlyEqual(p, Vec3{ 5.0f, 2.0f, 0.0f }));

    // Identity transform is a no-op.
    Transform identity;
    CHECK(NearlyEqual(TransformPoint(Vec3{ 7.0f, 8.0f, 9.0f }, identity.ToMatrix()),
                      Vec3{ 7.0f, 8.0f, 9.0f }));
}

// --- Math: Mat4 determinant / inverse --------------------------------------

TEST_CASE("math: Mat4 determinant")
{
    CHECK(NearlyEqual(Determinant(Mat4::Identity()), 1.0f));
    CHECK(NearlyEqual(Determinant(Mat4::Scale(Vec3{ 2.0f, 3.0f, 4.0f })), 24.0f));
}

TEST_CASE("math: Mat4 inverse undoes the transform")
{
    Transform xform;
    xform.scale = Vec3{ 2.0f, 0.5f, 3.0f };
    xform.rotation = Quat::FromAxisAngle(Normalized(Vec3{ 1.0f, 2.0f, 3.0f }), DegreesToRadians(50.0f));
    xform.position = Vec3{ 5.0f, -2.0f, 1.0f };

    const Mat4 m = xform.ToMatrix();
    const Mat4 inv = Inverse(m);

    CHECK(NearlyEqual(m * inv, Mat4::Identity(), 1.0e-3f));
    CHECK(NearlyEqual(inv * m, Mat4::Identity(), 1.0e-3f));

    // A point transformed then inverse-transformed returns to itself.
    const Vec3 p{ 3.0f, 4.0f, 5.0f };
    const Vec3 roundTrip = TransformPoint(TransformPoint(p, m), inv);
    CHECK(NearlyEqual(roundTrip, p, 1.0e-3f));

    // Singular matrix -> Identity (no divide-by-zero).
    CHECK(NearlyEqual(Inverse(Mat4::Scale(Vec3::Zero)), Mat4::Identity()));
}

// --- Math: Quat Slerp ------------------------------------------------------

TEST_CASE("math: Quat Slerp endpoints and midpoint")
{
    const Quat a = Quat::Identity;
    const Quat b = Quat::FromAxisAngle(Vec3::UnitZ, DegreesToRadians(90.0f));

    CHECK(NearlyEqual(Slerp(a, b, 0.0f), a));
    CHECK(NearlyEqual(Slerp(a, b, 1.0f), b));

    // Halfway between 0 and 90 deg about Z is 45 deg: maps +X to (cos45, sin45, 0).
    const Quat mid = Slerp(a, b, 0.5f);
    const Vec3 rotated = RotateVector(mid, Vec3::UnitX);
    const f32 c = Cos(DegreesToRadians(45.0f));
    CHECK(NearlyEqual(rotated, Vec3{ c, c, 0.0f }, 1.0e-4f));
}

// --- Math: Vec2 / Vec4 normalize -------------------------------------------

TEST_CASE("math: Vec2 and Vec4 Normalized")
{
    CHECK(NearlyEqual(Length(Normalized(Vec2{ 3.0f, 4.0f })), 1.0f));
    CHECK(Normalized(Vec2::Zero) == Vec2::Zero);

    CHECK(NearlyEqual(Length(Normalized(Vec4{ 1.0f, 2.0f, 2.0f, 4.0f })), 1.0f));
    CHECK(Normalized(Vec4::Zero) == Vec4::Zero);
}

// --- Math: Mat3 ------------------------------------------------------------

TEST_CASE("math: Mat3 identity, multiply, transpose")
{
    const Mat3 id = Mat3::Identity();
    const Mat3 r = Mat3::FromMat4(Mat4::RotationZ(DegreesToRadians(90.0f)));

    CHECK(NearlyEqual(id * r, r));
    CHECK(NearlyEqual(Transpose(Transpose(r)), r));

    static_assert(Mat3::Identity()(1, 1) == 1.0f);
}

TEST_CASE("math: Mat3 row-vector rotation matches Mat4")
{
    const Mat3 rz = Mat3::FromMat4(Mat4::RotationZ(DegreesToRadians(90.0f)));
    CHECK(NearlyEqual(Vec3::UnitX * rz, Vec3::UnitY));
}

TEST_CASE("math: Mat3 determinant and inverse")
{
    const Mat3 rz = Mat3::FromMat4(Mat4::RotationZ(DegreesToRadians(37.0f)));
    CHECK(NearlyEqual(Determinant(rz), 1.0f)); // pure rotation

    const Mat3 inv = Inverse(rz);
    CHECK(NearlyEqual(rz * inv, Mat3::Identity(), 1.0e-4f));

    // For a rotation, the inverse equals the transpose.
    CHECK(NearlyEqual(inv, Transpose(rz), 1.0e-4f));

    // Singular -> Identity.
    Mat3 zero{};
    CHECK(NearlyEqual(Inverse(zero), Mat3::Identity()));
}
