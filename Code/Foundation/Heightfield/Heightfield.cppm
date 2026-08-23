/// Foundation::Heightfield - `foundation.heightfield`.
///
/// The pure heightfield grid + sampling math, the shared source of truth the terrain renderer,
/// physics collider, and (later) the navigation bake all consume - none of them depending on the
/// terrain renderer to read heights. NO RHI, NO terrain concepts; depends only on Foundation::Core.
///
/// A grid is SQUARE with side S = 64*k + 1 (k >= 1: 65, 129, 257, ...): that one rule tiles into
/// 64-quad (65-vert) render chunks sharing edges AND satisfies Jolt's square HeightFieldShape. u16
/// samples map linearly onto a world Y range [minY, maxY] over a worldSize (X by Z) footprint centred
/// on the LOCAL origin (XZ plane, +Y up); the entity transform places it in the world.

module;
#include "Core/Prelude.h"
#include "Core/Reflection/Reflect.h"

export module foundation.heightfield;

import foundation.core;

using namespace foundation::core;

export namespace foundation::heightfield
{
    /// One quantized height sample (0 = minY, 65535 = maxY).
    using Height = u16;

    /// True when `size` is a legal heightfield side: square grids of S = 64*k + 1, k >= 1.
    [[nodiscard]] constexpr bool IsValidSize(i32 size) noexcept
    {
        return size >= 65 && ((size - 1) % 64) == 0;
    }

    /// The next legal size >= `size` (a convenience for import resampling; never below 65).
    [[nodiscard]] constexpr i32 NextValidSize(i32 size) noexcept
    {
        if (size <= 65)
        {
            return 65;
        }
        const i32 chunks = (size - 1 + 63) / 64; // ceil((size-1)/64)
        return chunks * 64 + 1;
    }

    /// A square heightfield: S*S u16 samples (row-major, index = gx + gz*S) over a world footprint.
    ///
    /// It IS-A Object so it can be a referenceable resource product (Ref<Heightfield>), the way
    /// StaticMesh is - terrain, the physics collider, and the nav bake all hold a Ref to one. The
    /// grid + sampling math still depend only on Foundation::Core; the cooked source + factory live
    /// in foundation.heightfield.resource.
    class Heightfield : public Object
    {
        RTTI_OBJECT(Heightfield, Object)
    public:
        // Unique per-OBJECT id: GPU caches (the terrain height-texture cache) key by THIS,
        // never by pointer - a freed heightfield's address can be reused by a fresh grid at
        // the same version (fresh grids all start at 1), and pointer keying would then serve
        // the dead grid's texture. The StaticMesh::uid precedent (bind-group-cache rule).
        const u64 uid = NextUid();

        [[nodiscard]] static u64 NextUid() noexcept
        {
            static Atomic<u64> counter{0};
            return counter.fetch_add(1) + 1;
        }

        Heightfield() = default;

        /// Allocate a zeroed grid. `size` MUST satisfy IsValidSize; worldSize is the XZ footprint,
        /// [minY, maxY] the world Y range the u16 samples span (maxY > minY).
        Heightfield(i32 size, Float2 worldSize, f32 minY, f32 maxY)
            : m_size(size), m_worldSize(worldSize), m_minY(minY), m_maxY(maxY)
        {
            m_samples.Resize(static_cast<usize>(size) * static_cast<usize>(size), Height{0});
        }

        [[nodiscard]] bool IsEmpty() const noexcept { return m_size <= 0; }
        [[nodiscard]] i32 Size() const noexcept { return m_size; }
        [[nodiscard]] Float2 WorldSize() const noexcept { return m_worldSize; }
        [[nodiscard]] f32 MinY() const noexcept { return m_minY; }
        [[nodiscard]] f32 MaxY() const noexcept { return m_maxY; }

        /// Monotonic edit generation (starts at 1). Bump it after rewriting samples so downstream
        /// GPU caches (the terrain height texture) re-upload - the sculpt/regen path.
        [[nodiscard]] u64 Version() const noexcept { return m_version; }
        void BumpVersion() noexcept { ++m_version; }

        // ---- raw sample access (grid indices clamped to [0, S-1]) ----
        [[nodiscard]] Height GetSample(i32 gx, i32 gz) const noexcept
        {
            return m_samples[Index(gx, gz)];
        }
        void SetSample(i32 gx, i32 gz, Height h) noexcept { m_samples[Index(gx, gz)] = h; }

        [[nodiscard]] Span<const Height> Samples() const noexcept
        {
            return Span<const Height>{m_samples.Data(), m_samples.Size()};
        }
        [[nodiscard]] Span<Height> Samples() noexcept
        {
            return Span<Height>{m_samples.Data(), m_samples.Size()};
        }

        // ---- quantization (sample <-> world Y) ----
        [[nodiscard]] f32 SampleToWorldY(f32 sample) const noexcept
        {
            return m_minY + (sample / 65535.0f) * (m_maxY - m_minY);
        }
        [[nodiscard]] Height WorldYToSample(f32 worldY) const noexcept
        {
            const f32 range = m_maxY - m_minY;
            const f32 t = (range > 0.0f) ? (worldY - m_minY) / range : 0.0f;
            const f32 clamped = Clamp01(t) * 65535.0f;
            return static_cast<Height>(Round(clamped));
        }

        // ---- world XZ <-> grid coordinate (grid space is [0, S-1], footprint centred on origin) ----
        [[nodiscard]] Float2 WorldToGrid(f32 worldX, f32 worldZ) const noexcept
        {
            const f32 span = static_cast<f32>(m_size - 1);
            const f32 gx = (worldX / m_worldSize.x + 0.5f) * span;
            const f32 gz = (worldZ / m_worldSize.y + 0.5f) * span;
            return Float2{gx, gz};
        }
        [[nodiscard]] Float2 GridToWorld(f32 gridX, f32 gridZ) const noexcept
        {
            const f32 span = static_cast<f32>(m_size - 1);
            const f32 wx = (gridX / span - 0.5f) * m_worldSize.x;
            const f32 wz = (gridZ / span - 0.5f) * m_worldSize.y;
            return Float2{wx, wz};
        }

        // ---- height queries (world Y) ----
        /// Nearest grid point height (exact at grid samples).
        [[nodiscard]] f32 GetHeightAtGrid(i32 gx, i32 gz) const noexcept
        {
            return SampleToWorldY(static_cast<f32>(GetSample(gx, gz)));
        }

        /// Bilinear height at a world XZ point; positions outside the footprint clamp to the edge.
        [[nodiscard]] f32 GetHeightAt(f32 worldX, f32 worldZ) const noexcept
        {
            if (IsEmpty())
            {
                return 0.0f;
            }
            const Float2 g = WorldToGrid(worldX, worldZ);
            const f32 span = static_cast<f32>(m_size - 1);
            const f32 cgx = Clamp(g.x, 0.0f, span);
            const f32 cgz = Clamp(g.y, 0.0f, span);
            const i32 x0 = Clamp(static_cast<i32>(Floor(cgx)), 0, m_size - 2 < 0 ? 0 : m_size - 2);
            const i32 z0 = Clamp(static_cast<i32>(Floor(cgz)), 0, m_size - 2 < 0 ? 0 : m_size - 2);
            const f32 fx = cgx - static_cast<f32>(x0);
            const f32 fz = cgz - static_cast<f32>(z0);
            const f32 h00 = static_cast<f32>(GetSample(x0, z0));
            const f32 h10 = static_cast<f32>(GetSample(x0 + 1, z0));
            const f32 h01 = static_cast<f32>(GetSample(x0, z0 + 1));
            const f32 h11 = static_cast<f32>(GetSample(x0 + 1, z0 + 1));
            const f32 top = h00 + (h10 - h00) * fx;
            const f32 bottom = h01 + (h11 - h01) * fx;
            return SampleToWorldY(top + (bottom - top) * fz);
        }

        /// Surface normal (world space, unit) via central differences one cell wide.
        [[nodiscard]] Float3 GetNormalAt(f32 worldX, f32 worldZ) const noexcept
        {
            if (IsEmpty())
            {
                return Float3{0.0f, 1.0f, 0.0f};
            }
            const f32 ex = m_worldSize.x / static_cast<f32>(m_size - 1);
            const f32 ez = m_worldSize.y / static_cast<f32>(m_size - 1);
            const f32 dhdx = (GetHeightAt(worldX + ex, worldZ) - GetHeightAt(worldX - ex, worldZ)) /
                             (2.0f * ex);
            const f32 dhdz = (GetHeightAt(worldX, worldZ + ez) - GetHeightAt(worldX, worldZ - ez)) /
                             (2.0f * ez);
            return Normalized(Float3{-dhdx, 1.0f, -dhdz});
        }

        /// Min/max world Y over the inclusive grid block [gx0..gx1] x [gz0..gz1] (chunk bounds).
        void CellBounds(i32 gx0, i32 gz0, i32 gx1, i32 gz1, f32& outMinY, f32& outMaxY) const noexcept
        {
            Height lo = 65535;
            Height hi = 0;
            for (i32 z = gz0; z <= gz1; ++z)
            {
                for (i32 x = gx0; x <= gx1; ++x)
                {
                    const Height s = GetSample(x, z);
                    lo = s < lo ? s : lo;
                    hi = s > hi ? s : hi;
                }
            }
            outMinY = SampleToWorldY(static_cast<f32>(lo));
            outMaxY = SampleToWorldY(static_cast<f32>(hi));
        }

        /// Local-space ray against the surface. Returns the nearest hit distance in `outT` (>= 0).
        /// A height-field ray march: clip to the grid AABB, step by ~half a cell, and bisect the
        /// crossing where the ray drops to/through the bilinear surface. Robust for editor picking.
        [[nodiscard]] bool QueryRay(Float3 origin, Float3 direction, f32& outT) const noexcept
        {
            if (IsEmpty())
            {
                return false;
            }
            const Float3 dir = Normalized(direction);
            const f32 halfX = m_worldSize.x * 0.5f;
            const f32 halfZ = m_worldSize.y * 0.5f;
            f32 t0 = 0.0f;
            f32 t1 = 0.0f;
            if (!ClipToAabb(origin, dir, Float3{-halfX, m_minY, -halfZ}, Float3{halfX, m_maxY, halfZ},
                            t0, t1))
            {
                return false;
            }

            // Step so horizontal travel is ~half a cell; for near-vertical rays fall back to Y range.
            const f32 cell = m_worldSize.x / static_cast<f32>(m_size - 1);
            const f32 horizSpeed = Sqrt(dir.x * dir.x + dir.z * dir.z);
            const f32 vertSpeed = Abs(dir.y);
            const f32 speed = horizSpeed > vertSpeed ? horizSpeed : vertSpeed;
            const f32 step = (speed > 1.0e-6f) ? (0.5f * cell / speed) : (0.5f * cell);

            f32 tPrev = t0;
            f32 diffPrev = SignedGap(origin, dir, t0);
            if (diffPrev <= 0.0f) // already at/under the surface at entry
            {
                outT = t0;
                return true;
            }
            for (f32 t = t0 + step; t <= t1 + step; t += step)
            {
                const f32 tc = t < t1 ? t : t1;
                const f32 diff = SignedGap(origin, dir, tc);
                if (diff <= 0.0f) // crossed the surface between tPrev and tc
                {
                    f32 lo = tPrev;
                    f32 hi = tc;
                    for (i32 i = 0; i < 12; ++i) // bisect to refine
                    {
                        const f32 mid = 0.5f * (lo + hi);
                        if (SignedGap(origin, dir, mid) > 0.0f)
                        {
                            lo = mid;
                        }
                        else
                        {
                            hi = mid;
                        }
                    }
                    outT = 0.5f * (lo + hi);
                    return true;
                }
                tPrev = tc;
                diffPrev = diff;
                if (tc >= t1)
                {
                    break;
                }
            }
            return false;
        }

    private:
        [[nodiscard]] usize Index(i32 gx, i32 gz) const noexcept
        {
            const i32 cx = Clamp(gx, 0, m_size - 1);
            const i32 cz = Clamp(gz, 0, m_size - 1);
            return static_cast<usize>(cx) + static_cast<usize>(cz) * static_cast<usize>(m_size);
        }

        // Ray height above the surface at distance t (positive above, negative below).
        [[nodiscard]] f32 SignedGap(Float3 origin, Float3 dir, f32 t) const noexcept
        {
            const f32 px = origin.x + dir.x * t;
            const f32 py = origin.y + dir.y * t;
            const f32 pz = origin.z + dir.z * t;
            return py - GetHeightAt(px, pz);
        }

        // Slab clip of a ray against an AABB; returns the [t0, t1] overlap (t0 >= 0), false if none.
        [[nodiscard]] static bool ClipToAabb(Float3 o, Float3 d, Float3 lo, Float3 hi, f32& t0,
                                             f32& t1) noexcept
        {
            f32 tmin = 0.0f;
            f32 tmax = 3.402823466e38f;
            const f32 oa[3] = {o.x, o.y, o.z};
            const f32 da[3] = {d.x, d.y, d.z};
            const f32 la[3] = {lo.x, lo.y, lo.z};
            const f32 ha[3] = {hi.x, hi.y, hi.z};
            for (i32 a = 0; a < 3; ++a)
            {
                if (Abs(da[a]) < 1.0e-8f)
                {
                    if (oa[a] < la[a] || oa[a] > ha[a])
                    {
                        return false; // parallel and outside the slab
                    }
                }
                else
                {
                    const f32 inv = 1.0f / da[a];
                    f32 tNear = (la[a] - oa[a]) * inv;
                    f32 tFar = (ha[a] - oa[a]) * inv;
                    if (tNear > tFar)
                    {
                        const f32 tmp = tNear;
                        tNear = tFar;
                        tFar = tmp;
                    }
                    tmin = tNear > tmin ? tNear : tmin;
                    tmax = tFar < tmax ? tFar : tmax;
                    if (tmin > tmax)
                    {
                        return false;
                    }
                }
            }
            t0 = tmin;
            t1 = tmax;
            return true;
        }

        [[nodiscard]] static f32 Clamp(f32 v, f32 lo, f32 hi) noexcept
        {
            return v < lo ? lo : (v > hi ? hi : v);
        }
        [[nodiscard]] static i32 Clamp(i32 v, i32 lo, i32 hi) noexcept
        {
            return v < lo ? lo : (v > hi ? hi : v);
        }
        [[nodiscard]] static f32 Clamp01(f32 v) noexcept { return Clamp(v, 0.0f, 1.0f); }

        i32 m_size = 0;
        Float2 m_worldSize{0.0f, 0.0f};
        f32 m_minY = 0.0f;
        f32 m_maxY = 0.0f;
        u64 m_version = 1; // edit generation (BumpVersion) for GPU-cache invalidation
        Array<Height> m_samples;
    };

    RTTI_DEFINE_OBJECT(Heightfield, "rtti::heightfield")
}
