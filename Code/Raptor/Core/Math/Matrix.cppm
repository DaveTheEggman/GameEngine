// Raptor Core — :matrix partition (Mat4, Quat, Transform)
//
// Conventions (Documentation/Planning/Core.md §7):
//   * Row-major storage: m[row][col].
//   * Row vectors: transform is v' = v * M (vector on the left).
//   * Composition reads left-to-right: v * World * View * Proj.
//   * XNA-style right-handed projections; NDC depth in [0, 1].
// Translation lives in the last ROW (m[3][0..2]); these match XNA's matrices.

module;
#include "Core/Prelude.h"
#include "Core/Debug/Assert.h"

export module raptor.core:matrix;

import :base;
import :math;

export namespace raptor::core
{
    // =======================================================================
    // Mat4 — 4x4, row-major, row-vector convention.
    // =======================================================================
    struct Mat4
    {
        f32 m[4][4];

        [[nodiscard]] constexpr f32 operator()(usize row, usize col) const noexcept
        {
            RAPTOR_ASSERT(row < 4 && col < 4);
            return m[row][col];
        }
        [[nodiscard]] constexpr f32& operator()(usize row, usize col) noexcept
        {
            RAPTOR_ASSERT(row < 4 && col < 4);
            return m[row][col];
        }

        [[nodiscard]] static constexpr Mat4 Identity() noexcept
        {
            return Mat4{ { { 1.0f, 0.0f, 0.0f, 0.0f },
                           { 0.0f, 1.0f, 0.0f, 0.0f },
                           { 0.0f, 0.0f, 1.0f, 0.0f },
                           { 0.0f, 0.0f, 0.0f, 1.0f } } };
        }

        [[nodiscard]] static constexpr Mat4 Translation(Vec3 t) noexcept
        {
            return Mat4{ { { 1.0f, 0.0f, 0.0f, 0.0f },
                           { 0.0f, 1.0f, 0.0f, 0.0f },
                           { 0.0f, 0.0f, 1.0f, 0.0f },
                           { t.x,  t.y,  t.z,  1.0f } } };
        }

        [[nodiscard]] static constexpr Mat4 Scale(Vec3 s) noexcept
        {
            return Mat4{ { { s.x,  0.0f, 0.0f, 0.0f },
                           { 0.0f, s.y,  0.0f, 0.0f },
                           { 0.0f, 0.0f, s.z,  0.0f },
                           { 0.0f, 0.0f, 0.0f, 1.0f } } };
        }

        [[nodiscard]] static Mat4 RotationX(f32 radians) noexcept
        {
            const f32 c = Cos(radians);
            const f32 s = Sin(radians);
            return Mat4{ { { 1.0f, 0.0f, 0.0f, 0.0f },
                           { 0.0f, c,    s,    0.0f },
                           { 0.0f, -s,   c,    0.0f },
                           { 0.0f, 0.0f, 0.0f, 1.0f } } };
        }

        [[nodiscard]] static Mat4 RotationY(f32 radians) noexcept
        {
            const f32 c = Cos(radians);
            const f32 s = Sin(radians);
            return Mat4{ { { c,    0.0f, -s,   0.0f },
                           { 0.0f, 1.0f, 0.0f, 0.0f },
                           { s,    0.0f, c,    0.0f },
                           { 0.0f, 0.0f, 0.0f, 1.0f } } };
        }

        [[nodiscard]] static Mat4 RotationZ(f32 radians) noexcept
        {
            const f32 c = Cos(radians);
            const f32 s = Sin(radians);
            return Mat4{ { { c,    s,    0.0f, 0.0f },
                           { -s,   c,    0.0f, 0.0f },
                           { 0.0f, 0.0f, 1.0f, 0.0f },
                           { 0.0f, 0.0f, 0.0f, 1.0f } } };
        }

        // Right-handed perspective, NDC z in [0, 1] (XNA / D3D style).
        [[nodiscard]] static Mat4 PerspectiveFovRH(f32 fovYRadians, f32 aspect, f32 zNear, f32 zFar) noexcept
        {
            const f32 yScale = 1.0f / Tan(fovYRadians * 0.5f);
            const f32 xScale = yScale / aspect;
            const f32 zRange = zFar / (zNear - zFar);
            return Mat4{ { { xScale, 0.0f,   0.0f,           0.0f },
                           { 0.0f,   yScale, 0.0f,           0.0f },
                           { 0.0f,   0.0f,   zRange,         -1.0f },
                           { 0.0f,   0.0f,   zNear * zRange, 0.0f } } };
        }

        [[nodiscard]] static Mat4 OrthographicRH(f32 width, f32 height, f32 zNear, f32 zFar) noexcept
        {
            const f32 zRange = 1.0f / (zNear - zFar);
            return Mat4{ { { 2.0f / width, 0.0f,          0.0f,           0.0f },
                           { 0.0f,         2.0f / height, 0.0f,           0.0f },
                           { 0.0f,         0.0f,          zRange,         0.0f },
                           { 0.0f,         0.0f,          zNear * zRange, 1.0f } } };
        }

        [[nodiscard]] static Mat4 LookAtRH(Vec3 eye, Vec3 target, Vec3 up) noexcept
        {
            const Vec3 zAxis = Normalized(eye - target); // camera looks down -z
            const Vec3 xAxis = Normalized(Cross(up, zAxis));
            const Vec3 yAxis = Cross(zAxis, xAxis);
            return Mat4{ { { xAxis.x, yAxis.x, zAxis.x, 0.0f },
                           { xAxis.y, yAxis.y, zAxis.y, 0.0f },
                           { xAxis.z, yAxis.z, zAxis.z, 0.0f },
                           { -Dot(xAxis, eye), -Dot(yAxis, eye), -Dot(zAxis, eye), 1.0f } } };
        }
    };

    [[nodiscard]] constexpr Mat4 operator*(const Mat4& a, const Mat4& b) noexcept
    {
        Mat4 result{};
        for (usize row = 0; row < 4; ++row)
        {
            for (usize col = 0; col < 4; ++col)
            {
                f32 sum = 0.0f;
                for (usize k = 0; k < 4; ++k)
                {
                    sum += a.m[row][k] * b.m[k][col];
                }
                result.m[row][col] = sum;
            }
        }
        return result;
    }

    // Row-vector transform: v' = v * M.
    [[nodiscard]] constexpr Vec4 operator*(Vec4 v, const Mat4& m) noexcept
    {
        return { v.x * m.m[0][0] + v.y * m.m[1][0] + v.z * m.m[2][0] + v.w * m.m[3][0],
                 v.x * m.m[0][1] + v.y * m.m[1][1] + v.z * m.m[2][1] + v.w * m.m[3][1],
                 v.x * m.m[0][2] + v.y * m.m[1][2] + v.z * m.m[2][2] + v.w * m.m[3][2],
                 v.x * m.m[0][3] + v.y * m.m[1][3] + v.z * m.m[2][3] + v.w * m.m[3][3] };
    }

    [[nodiscard]] constexpr Mat4 Transpose(const Mat4& a) noexcept
    {
        Mat4 result{};
        for (usize row = 0; row < 4; ++row)
        {
            for (usize col = 0; col < 4; ++col)
            {
                result.m[row][col] = a.m[col][row];
            }
        }
        return result;
    }

    // Transforms a position (implicit w = 1, translation applied).
    [[nodiscard]] constexpr Vec3 TransformPoint(Vec3 p, const Mat4& m) noexcept
    {
        return { p.x * m.m[0][0] + p.y * m.m[1][0] + p.z * m.m[2][0] + m.m[3][0],
                 p.x * m.m[0][1] + p.y * m.m[1][1] + p.z * m.m[2][1] + m.m[3][1],
                 p.x * m.m[0][2] + p.y * m.m[1][2] + p.z * m.m[2][2] + m.m[3][2] };
    }

    // Transforms a direction (implicit w = 0, translation ignored).
    [[nodiscard]] constexpr Vec3 TransformDirection(Vec3 d, const Mat4& m) noexcept
    {
        return { d.x * m.m[0][0] + d.y * m.m[1][0] + d.z * m.m[2][0],
                 d.x * m.m[0][1] + d.y * m.m[1][1] + d.z * m.m[2][1],
                 d.x * m.m[0][2] + d.y * m.m[1][2] + d.z * m.m[2][2] };
    }

    [[nodiscard]] inline bool NearlyEqual(const Mat4& a, const Mat4& b, f32 epsilon = kEpsilon) noexcept
    {
        for (usize row = 0; row < 4; ++row)
        {
            for (usize col = 0; col < 4; ++col)
            {
                if (!NearlyEqual(a.m[row][col], b.m[row][col], epsilon)) { return false; }
            }
        }
        return true;
    }

    [[nodiscard]] inline f32 Determinant(const Mat4& mat) noexcept
    {
        const f32* m = &mat.m[0][0];
        const f32 s0 = m[0] * m[5] - m[1] * m[4];
        const f32 s1 = m[0] * m[6] - m[2] * m[4];
        const f32 s2 = m[0] * m[7] - m[3] * m[4];
        const f32 s3 = m[1] * m[6] - m[2] * m[5];
        const f32 s4 = m[1] * m[7] - m[3] * m[5];
        const f32 s5 = m[2] * m[7] - m[3] * m[6];
        const f32 c5 = m[10] * m[15] - m[11] * m[14];
        const f32 c4 = m[9] * m[15] - m[11] * m[13];
        const f32 c3 = m[9] * m[14] - m[10] * m[13];
        const f32 c2 = m[8] * m[15] - m[11] * m[12];
        const f32 c1 = m[8] * m[14] - m[10] * m[12];
        const f32 c0 = m[8] * m[13] - m[9] * m[12];
        return s0 * c5 - s1 * c4 + s2 * c3 + s3 * c2 - s4 * c1 + s5 * c0;
    }

    // Full 4x4 inverse (adjugate / determinant). Returns Identity for a
    // singular matrix.
    [[nodiscard]] inline Mat4 Inverse(const Mat4& mat) noexcept
    {
        const f32* m = &mat.m[0][0];
        f32 inv[16];

        inv[0]  =  m[5]*m[10]*m[15] - m[5]*m[11]*m[14] - m[9]*m[6]*m[15] + m[9]*m[7]*m[14] + m[13]*m[6]*m[11] - m[13]*m[7]*m[10];
        inv[4]  = -m[4]*m[10]*m[15] + m[4]*m[11]*m[14] + m[8]*m[6]*m[15] - m[8]*m[7]*m[14] - m[12]*m[6]*m[11] + m[12]*m[7]*m[10];
        inv[8]  =  m[4]*m[9]*m[15]  - m[4]*m[11]*m[13] - m[8]*m[5]*m[15] + m[8]*m[7]*m[13] + m[12]*m[5]*m[11] - m[12]*m[7]*m[9];
        inv[12] = -m[4]*m[9]*m[14]  + m[4]*m[10]*m[13] + m[8]*m[5]*m[14] - m[8]*m[6]*m[13] - m[12]*m[5]*m[10] + m[12]*m[6]*m[9];
        inv[1]  = -m[1]*m[10]*m[15] + m[1]*m[11]*m[14] + m[9]*m[2]*m[15] - m[9]*m[3]*m[14] - m[13]*m[2]*m[11] + m[13]*m[3]*m[10];
        inv[5]  =  m[0]*m[10]*m[15] - m[0]*m[11]*m[14] - m[8]*m[2]*m[15] + m[8]*m[3]*m[14] + m[12]*m[2]*m[11] - m[12]*m[3]*m[10];
        inv[9]  = -m[0]*m[9]*m[15]  + m[0]*m[11]*m[13] + m[8]*m[1]*m[15] - m[8]*m[3]*m[13] - m[12]*m[1]*m[11] + m[12]*m[3]*m[9];
        inv[13] =  m[0]*m[9]*m[14]  - m[0]*m[10]*m[13] - m[8]*m[1]*m[14] + m[8]*m[2]*m[13] + m[12]*m[1]*m[10] - m[12]*m[2]*m[9];
        inv[2]  =  m[1]*m[6]*m[15]  - m[1]*m[7]*m[14]  - m[5]*m[2]*m[15] + m[5]*m[3]*m[14] + m[13]*m[2]*m[7]  - m[13]*m[3]*m[6];
        inv[6]  = -m[0]*m[6]*m[15]  + m[0]*m[7]*m[14]  + m[4]*m[2]*m[15] - m[4]*m[3]*m[14] - m[12]*m[2]*m[7]  + m[12]*m[3]*m[6];
        inv[10] =  m[0]*m[5]*m[15]  - m[0]*m[7]*m[13]  - m[4]*m[1]*m[15] + m[4]*m[3]*m[13] + m[12]*m[1]*m[7]  - m[12]*m[3]*m[5];
        inv[14] = -m[0]*m[5]*m[14]  + m[0]*m[6]*m[13]  + m[4]*m[1]*m[14] - m[4]*m[2]*m[13] - m[12]*m[1]*m[6]  + m[12]*m[2]*m[5];
        inv[3]  = -m[1]*m[6]*m[11]  + m[1]*m[7]*m[10]  + m[5]*m[2]*m[11] - m[5]*m[3]*m[10] - m[9]*m[2]*m[7]   + m[9]*m[3]*m[6];
        inv[7]  =  m[0]*m[6]*m[11]  - m[0]*m[7]*m[10]  - m[4]*m[2]*m[11] + m[4]*m[3]*m[10] + m[8]*m[2]*m[7]   - m[8]*m[3]*m[6];
        inv[11] = -m[0]*m[5]*m[11]  + m[0]*m[7]*m[9]   + m[4]*m[1]*m[11] - m[4]*m[3]*m[9]  - m[8]*m[1]*m[7]   + m[8]*m[3]*m[5];
        inv[15] =  m[0]*m[5]*m[10]  - m[0]*m[6]*m[9]   - m[4]*m[1]*m[10] + m[4]*m[2]*m[9]  + m[8]*m[1]*m[6]   - m[8]*m[2]*m[5];

        f32 det = m[0]*inv[0] + m[1]*inv[4] + m[2]*inv[8] + m[3]*inv[12];
        if (NearlyZero(det))
        {
            return Mat4::Identity();
        }

        const f32 invDet = 1.0f / det;
        Mat4 result{};
        f32* out = &result.m[0][0];
        for (usize i = 0; i < 16; ++i)
        {
            out[i] = inv[i] * invDet;
        }
        return result;
    }

    // =======================================================================
    // Mat3 — 3x3, row-major, row-vector convention. Rotation / normal matrices.
    // =======================================================================
    struct Mat3
    {
        f32 m[3][3];

        [[nodiscard]] constexpr f32 operator()(usize row, usize col) const noexcept
        {
            RAPTOR_ASSERT(row < 3 && col < 3);
            return m[row][col];
        }
        [[nodiscard]] constexpr f32& operator()(usize row, usize col) noexcept
        {
            RAPTOR_ASSERT(row < 3 && col < 3);
            return m[row][col];
        }

        [[nodiscard]] static constexpr Mat3 Identity() noexcept
        {
            return Mat3{ { { 1.0f, 0.0f, 0.0f },
                           { 0.0f, 1.0f, 0.0f },
                           { 0.0f, 0.0f, 1.0f } } };
        }

        // Upper-left 3x3 of a Mat4 (drops translation; the rotation/scale part).
        [[nodiscard]] static constexpr Mat3 FromMat4(const Mat4& mat) noexcept
        {
            return Mat3{ { { mat.m[0][0], mat.m[0][1], mat.m[0][2] },
                           { mat.m[1][0], mat.m[1][1], mat.m[1][2] },
                           { mat.m[2][0], mat.m[2][1], mat.m[2][2] } } };
        }
    };

    [[nodiscard]] constexpr Mat3 operator*(const Mat3& a, const Mat3& b) noexcept
    {
        Mat3 result{};
        for (usize row = 0; row < 3; ++row)
        {
            for (usize col = 0; col < 3; ++col)
            {
                f32 sum = 0.0f;
                for (usize k = 0; k < 3; ++k) { sum += a.m[row][k] * b.m[k][col]; }
                result.m[row][col] = sum;
            }
        }
        return result;
    }

    // Row-vector transform: v' = v * M.
    [[nodiscard]] constexpr Vec3 operator*(Vec3 v, const Mat3& m) noexcept
    {
        return { v.x * m.m[0][0] + v.y * m.m[1][0] + v.z * m.m[2][0],
                 v.x * m.m[0][1] + v.y * m.m[1][1] + v.z * m.m[2][1],
                 v.x * m.m[0][2] + v.y * m.m[1][2] + v.z * m.m[2][2] };
    }

    [[nodiscard]] constexpr Mat3 Transpose(const Mat3& a) noexcept
    {
        Mat3 result{};
        for (usize row = 0; row < 3; ++row)
        {
            for (usize col = 0; col < 3; ++col) { result.m[row][col] = a.m[col][row]; }
        }
        return result;
    }

    [[nodiscard]] constexpr f32 Determinant(const Mat3& m) noexcept
    {
        return m.m[0][0] * (m.m[1][1] * m.m[2][2] - m.m[1][2] * m.m[2][1])
             - m.m[0][1] * (m.m[1][0] * m.m[2][2] - m.m[1][2] * m.m[2][0])
             + m.m[0][2] * (m.m[1][0] * m.m[2][1] - m.m[1][1] * m.m[2][0]);
    }

    // 3x3 inverse (adjugate / determinant). Returns Identity if singular.
    [[nodiscard]] inline Mat3 Inverse(const Mat3& m) noexcept
    {
        const f32 det = Determinant(m);
        if (NearlyZero(det)) { return Mat3::Identity(); }
        const f32 invDet = 1.0f / det;

        Mat3 result{};
        result.m[0][0] = (m.m[1][1] * m.m[2][2] - m.m[1][2] * m.m[2][1]) * invDet;
        result.m[0][1] = (m.m[0][2] * m.m[2][1] - m.m[0][1] * m.m[2][2]) * invDet;
        result.m[0][2] = (m.m[0][1] * m.m[1][2] - m.m[0][2] * m.m[1][1]) * invDet;
        result.m[1][0] = (m.m[1][2] * m.m[2][0] - m.m[1][0] * m.m[2][2]) * invDet;
        result.m[1][1] = (m.m[0][0] * m.m[2][2] - m.m[0][2] * m.m[2][0]) * invDet;
        result.m[1][2] = (m.m[0][2] * m.m[1][0] - m.m[0][0] * m.m[1][2]) * invDet;
        result.m[2][0] = (m.m[1][0] * m.m[2][1] - m.m[1][1] * m.m[2][0]) * invDet;
        result.m[2][1] = (m.m[0][1] * m.m[2][0] - m.m[0][0] * m.m[2][1]) * invDet;
        result.m[2][2] = (m.m[0][0] * m.m[1][1] - m.m[0][1] * m.m[1][0]) * invDet;
        return result;
    }

    [[nodiscard]] inline bool NearlyEqual(const Mat3& a, const Mat3& b, f32 epsilon = kEpsilon) noexcept
    {
        for (usize row = 0; row < 3; ++row)
        {
            for (usize col = 0; col < 3; ++col)
            {
                if (!NearlyEqual(a.m[row][col], b.m[row][col], epsilon)) { return false; }
            }
        }
        return true;
    }

    // =======================================================================
    // Quat — unit quaternion rotation (x, y, z, w).
    // =======================================================================
    struct Quat
    {
        f32 x = 0.0f;
        f32 y = 0.0f;
        f32 z = 0.0f;
        f32 w = 1.0f;

        constexpr Quat() noexcept = default;
        constexpr Quat(f32 inX, f32 inY, f32 inZ, f32 inW) noexcept : x(inX), y(inY), z(inZ), w(inW) {}

        [[nodiscard]] static Quat FromAxisAngle(Vec3 axis, f32 radians) noexcept
        {
            const f32 half = radians * 0.5f;
            const f32 s = Sin(half);
            const Vec3 a = Normalized(axis);
            return Quat{ a.x * s, a.y * s, a.z * s, Cos(half) };
        }

        static const Quat Identity;
    };

    inline constexpr Quat Quat::Identity{ 0.0f, 0.0f, 0.0f, 1.0f };

    // Hamilton product: applies `b` then `a` to a vector.
    [[nodiscard]] constexpr Quat operator*(Quat a, Quat b) noexcept
    {
        return { a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
                 a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
                 a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w,
                 a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z };
    }

    [[nodiscard]] constexpr Quat Conjugate(Quat q) noexcept { return { -q.x, -q.y, -q.z, q.w }; }
    [[nodiscard]] constexpr f32 Dot(Quat a, Quat b) noexcept { return a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w; }

    [[nodiscard]] inline Quat Normalized(Quat q) noexcept
    {
        const f32 lengthSq = Dot(q, q);
        if (lengthSq <= kEpsilon * kEpsilon) { return Quat::Identity; }
        const f32 inv = 1.0f / Sqrt(lengthSq);
        return { q.x * inv, q.y * inv, q.z * inv, q.w * inv };
    }

    [[nodiscard]] constexpr Vec3 RotateVector(Quat q, Vec3 v) noexcept
    {
        const Vec3 u{ q.x, q.y, q.z };
        const f32 s = q.w;
        return u * (2.0f * Dot(u, v)) + v * (s * s - Dot(u, u)) + Cross(u, v) * (2.0f * s);
    }

    [[nodiscard]] inline bool NearlyEqual(Quat a, Quat b, f32 epsilon = kEpsilon) noexcept
    {
        return NearlyEqual(a.x, b.x, epsilon) && NearlyEqual(a.y, b.y, epsilon)
            && NearlyEqual(a.z, b.z, epsilon) && NearlyEqual(a.w, b.w, epsilon);
    }

    // Spherical linear interpolation along the shortest arc; result is unit.
    [[nodiscard]] inline Quat Slerp(Quat a, Quat b, f32 t) noexcept
    {
        f32 cosTheta = Dot(a, b);
        if (cosTheta < 0.0f) // shortest path
        {
            b = Quat{ -b.x, -b.y, -b.z, -b.w };
            cosTheta = -cosTheta;
        }

        if (cosTheta > 0.9995f) // nearly parallel — lerp + normalize
        {
            return Normalized(Quat{ a.x + (b.x - a.x) * t,
                                    a.y + (b.y - a.y) * t,
                                    a.z + (b.z - a.z) * t,
                                    a.w + (b.w - a.w) * t });
        }

        const f32 theta0 = Acos(cosTheta);
        const f32 theta = theta0 * t;
        const f32 sinTheta = Sin(theta);
        const f32 sinTheta0 = Sin(theta0);
        const f32 s1 = sinTheta / sinTheta0;
        const f32 s0 = Cos(theta) - cosTheta * s1;
        return Quat{ a.x * s0 + b.x * s1,
                     a.y * s0 + b.y * s1,
                     a.z * s0 + b.z * s1,
                     a.w * s0 + b.w * s1 };
    }

    // Rotation matrix for a unit quaternion (row-vector convention, XNA layout).
    [[nodiscard]] constexpr Mat4 RotationMatrix(Quat q) noexcept
    {
        const f32 xx = q.x * q.x, yy = q.y * q.y, zz = q.z * q.z;
        const f32 xy = q.x * q.y, xz = q.x * q.z, yz = q.y * q.z;
        const f32 wx = q.w * q.x, wy = q.w * q.y, wz = q.w * q.z;
        return Mat4{ { { 1.0f - 2.0f * (yy + zz), 2.0f * (xy + wz),        2.0f * (xz - wy),        0.0f },
                       { 2.0f * (xy - wz),        1.0f - 2.0f * (xx + zz), 2.0f * (yz + wx),        0.0f },
                       { 2.0f * (xz + wy),        2.0f * (yz - wx),        1.0f - 2.0f * (xx + yy), 0.0f },
                       { 0.0f,                    0.0f,                    0.0f,                    1.0f } } };
    }

    // =======================================================================
    // Transform — position / rotation / scale, composed as S * R * T.
    // =======================================================================
    struct Transform
    {
        Vec3 position = Vec3::Zero;
        Quat rotation = Quat::Identity;
        Vec3 scale = Vec3::One;

        [[nodiscard]] Mat4 ToMatrix() const noexcept
        {
            Mat4 result = Mat4::Scale(scale) * RotationMatrix(rotation);
            result.m[3][0] = position.x;
            result.m[3][1] = position.y;
            result.m[3][2] = position.z;
            return result;
        }
    };
}
