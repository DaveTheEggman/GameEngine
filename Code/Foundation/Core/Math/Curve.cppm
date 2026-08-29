// Core - :curve partition
// A general scalar keyframe curve: an ordered set of {time, value, tangents, interpolation} keys
// sampled at an arbitrary time, using cubic-Hermite segment math and the :easings partition. Shared
// by any keyframe consumer. Float3/Color tracks hold one Curve per component; Quat tracks slerp
// quaternion keys directly and do NOT use this (never per-component euler curves).

module;
#include "Core/Prelude.h"

export module foundation.core:curve;

import :base;
import :math;
import :array;

export namespace foundation::core
{
    // How the segment LEAVING a key is interpolated toward the next key.
    enum class CurveKeyInterpolation : u8
    {
        Constant, // hold this key's value until the next key (step)
        Linear,   // straight lerp to the next key
        Cubic,    // cubic Hermite using tangentOut (this key) + tangentIn (next key)
    };

    // One keyframe. `tangentIn`/`tangentOut` are slopes (value units per time unit); they matter only
    // for Cubic segments (in = arriving at this key from the previous segment; out = leaving toward
    // the next). Constant/Linear ignore them.
    struct CurveKey
    {
        f32 time = 0.0f;
        f32 value = 0.0f;
        f32 tangentIn = 0.0f;
        f32 tangentOut = 0.0f;
        CurveKeyInterpolation interpolation = CurveKeyInterpolation::Linear;
    };

    // An ordered scalar curve. Keys are kept sorted by time (AddKey inserts in order); Evaluate
    // clamps to the end values outside [firstTime, lastTime] (loop/pingpong wrapping is the CLIP's
    // job, not the curve's). Cheap value type - copyable, no hidden state.
    class Curve
    {
    public:
        [[nodiscard]] usize KeyCount() const noexcept { return m_keys.Size(); }
        [[nodiscard]] const Array<CurveKey>& Keys() const noexcept { return m_keys; }
        [[nodiscard]] Array<CurveKey>& Keys() noexcept { return m_keys; }
        [[nodiscard]] bool IsEmpty() const noexcept { return m_keys.IsEmpty(); }

        // The curve's time span: the last key's time (0 when empty). The first key is expected at
        // t=0 for a normalized clip, but that is a convention, not enforced.
        [[nodiscard]] f32 Duration() const noexcept
        {
            return m_keys.IsEmpty() ? 0.0f : m_keys[m_keys.Size() - 1].time;
        }

        // Insert a key, keeping the array sorted by time (stable for equal times: the new key lands
        // after existing keys of the same time).
        void AddKey(const CurveKey& key)
        {
            usize i = m_keys.Size();
            while (i > 0 && m_keys[i - 1].time > key.time)
            {
                --i;
            }
            m_keys.Insert(i, key);
        }

        void Clear() noexcept { m_keys.Clear(); }

        // Sample the curve at `time`. Empty -> 0; single key -> its value; outside the key range ->
        // the nearest end value (clamped). Between two keys, the LEFT key's interpolation mode drives
        // the segment.
        [[nodiscard]] f32 Evaluate(f32 time) const noexcept
        {
            const usize count = m_keys.Size();
            if (count == 0)
            {
                return 0.0f;
            }
            if (count == 1 || time <= m_keys[0].time)
            {
                return m_keys[0].value;
            }
            if (time >= m_keys[count - 1].time)
            {
                return m_keys[count - 1].value;
            }

            // Find the segment [i, i+1] containing `time` (linear scan - key counts are small).
            usize i = 0;
            while (i + 1 < count && m_keys[i + 1].time <= time)
            {
                ++i;
            }
            const CurveKey& a = m_keys[i];
            const CurveKey& b = m_keys[i + 1];

            const f32 segment = b.time - a.time;
            if (segment <= 1e-6f)
            {
                return b.value; // coincident keys: jump to the later value
            }
            const f32 localT = (time - a.time) / segment;

            switch (a.interpolation)
            {
            case CurveKeyInterpolation::Constant:
                return a.value;
            case CurveKeyInterpolation::Linear:
                return a.value + (b.value - a.value) * localT;
            case CurveKeyInterpolation::Cubic:
                // Cubic Hermite; tangents are slopes, scaled by the segment length to the [0,1] basis.
                return Hermite(a.value, a.tangentOut * segment, b.value, b.tangentIn * segment, localT);
            }
            return a.value;
        }

    private:
        // Cubic Hermite basis: p0 at t=0, p1 at t=1, m0/m1 the endpoint tangents (already scaled).
        [[nodiscard]] static f32 Hermite(f32 p0, f32 m0, f32 p1, f32 m1, f32 t) noexcept
        {
            const f32 t2 = t * t;
            const f32 t3 = t2 * t;
            return (2.0f * t3 - 3.0f * t2 + 1.0f) * p0 + (t3 - 2.0f * t2 + t) * m0 +
                   (-2.0f * t3 + 3.0f * t2) * p1 + (t3 - t2) * m1;
        }

        Array<CurveKey> m_keys;
    };
}
