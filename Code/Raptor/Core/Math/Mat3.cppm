// Raptor Core — :mat3 partition
// Mat3: 3x3 row-major matrix (rotation / normal matrices) — multiply,
// Transpose/Determinant/Inverse, and FromMat4 (upper-left 3x3).
//
// Conventions (Documentation/Planning/Core.md §7): row-major storage m[row][col];
// row vectors (v' = v * M); composition left-to-right; XNA-style right-handed
// projections, NDC depth [0,1]; translation in the last row.

module;
#include "Core/Prelude.h"
#include "Core/Debug/Assert.h"

export module raptor.core:mat3;

import :base;
import :math;
import :vec3;
import :mat4;

export namespace raptor::core
{
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
}
