// Draconic::VG - :fills partition.
//
// Fill styles for vector-graphics shapes: the IVGFill interface, solid + linear/
// radial/conic gradient fills, gradient stops, and color interpolation helpers.
// Ported from Sedulous.VG (IVGFill/VGSolidFill/VG*GradientFill/GradientStop/
// ColorUtils). Colors are the engine's float Color (Sedulous used byte Color).

module;
#include "Draconic.Core/Prelude.h"

export module draconic.vg:fills;

import draconic.core;

using namespace draconic::core;

export namespace draconic::vg
{
    /// A color stop in a gradient at a normalized offset (0-1).
    struct GradientStop
    {
        f32 offset = 0.0f; ///< Position along the gradient (0 = start, 1 = end).
        Color color;       ///< Color at this stop.

        constexpr GradientStop() noexcept = default;
        constexpr GradientStop(f32 inOffset, Color inColor) noexcept
            : offset(inOffset), color(inColor)
        {
        }
    };

    /// Utility functions for color interpolation.
    class ColorUtils
    {
    public:
        /// Linearly interpolate between two colors.
        [[nodiscard]] static Color LerpColor(Color a, Color b, f32 t)
        {
            return Lerp(a, b, Clamp(t, 0.0f, 1.0f));
        }

        /// Interpolate through gradient stops at parameter t (0-1).
        [[nodiscard]] static Color InterpolateStops(Span<const GradientStop> stops, f32 t)
        {
            if (stops.Size() == 0)
                return Color::White;
            if (stops.Size() == 1)
                return stops[0].color;

            const f32 ct = Clamp(t, 0.0f, 1.0f);

            if (ct <= stops[0].offset)
                return stops[0].color;
            if (ct >= stops[stops.Size() - 1].offset)
                return stops[stops.Size() - 1].color;

            for (usize i = 0; i < stops.Size() - 1; ++i)
            {
                if (ct >= stops[i].offset && ct <= stops[i + 1].offset)
                {
                    const f32 range = stops[i + 1].offset - stops[i].offset;
                    if (range < 0.0001f)
                        return stops[i].color;
                    const f32 localT = (ct - stops[i].offset) / range;
                    return LerpColor(stops[i].color, stops[i + 1].color, localT);
                }
            }
            return stops[stops.Size() - 1].color;
        }
    };

    /// Interface for fill styles used to color vector graphics shapes.
    class IVGFill
    {
    public:
        virtual ~IVGFill() = default;

        /// Get the color at a specific point (for gradient interpolation).
        [[nodiscard]] virtual Color GetColorAt(Float2 position, Rectangle bounds) const = 0;
        /// Get the base/primary color of the fill.
        [[nodiscard]] virtual Color BaseColor() const = 0;
        /// Whether this fill requires per-vertex color interpolation.
        [[nodiscard]] virtual bool RequiresInterpolation() const = 0;

        /// The raw gradient parameter t at a point (linear projection, dist/radius, or
        /// angle/2PI), before clamping. Solid fills return 0. Used to bake + per-pixel
        /// address the gradient LUT (see [[vg-quality-track]]).
        [[nodiscard]] virtual f32 GetParameterAt(Float2 /*position*/, Rectangle /*bounds*/) const
        {
            return 0.0f;
        }
        /// Sample the fill's color ramp at parameter t (0-1). Solid fills return BaseColor.
        [[nodiscard]] virtual Color SampleRamp(f32 /*t*/) const { return BaseColor(); }
    };

    /// A solid color fill.
    class VGSolidFill final : public IVGFill
    {
    public:
        constexpr VGSolidFill() noexcept = default;
        explicit constexpr VGSolidFill(Color color) noexcept : m_color(color) {}

        [[nodiscard]] Color GetColorAt(Float2 /*position*/, Rectangle /*bounds*/) const override
        {
            return m_color;
        }
        [[nodiscard]] Color BaseColor() const override { return m_color; }
        [[nodiscard]] bool RequiresInterpolation() const override { return false; }

        /// Preset solid fills.
        [[nodiscard]] static VGSolidFill White() { return VGSolidFill(Color::White); }
        [[nodiscard]] static VGSolidFill Black() { return VGSolidFill(Color::Black); }
        [[nodiscard]] static VGSolidFill Red() { return VGSolidFill(Color::Red); }
        [[nodiscard]] static VGSolidFill Green() { return VGSolidFill(Color::Green); }
        [[nodiscard]] static VGSolidFill Blue() { return VGSolidFill(Color::Blue); }
        [[nodiscard]] static VGSolidFill Transparent() { return VGSolidFill(Color::Transparent); }

    private:
        Color m_color = Color::White;
    };

    /// Linear gradient fill between two points.
    class VGLinearGradientFill final : public IVGFill
    {
    public:
        Float2 startPoint;         ///< Start point of the gradient line.
        Float2 endPoint;           ///< End point of the gradient line.
        Array<GradientStop> stops; ///< Color stops defining the gradient.

        VGLinearGradientFill() = default;
        VGLinearGradientFill(Float2 inStart, Float2 inEnd) : startPoint(inStart), endPoint(inEnd) {}

        /// Add a color stop.
        void AddStop(f32 offset, Color color) { stops.PushBack(GradientStop(offset, color)); }

        [[nodiscard]] Color GetColorAt(Float2 position, Rectangle bounds) const override
        {
            return SampleRamp(GetParameterAt(position, bounds));
        }
        [[nodiscard]] f32 GetParameterAt(Float2 position, Rectangle /*bounds*/) const override
        {
            const Float2 gradientDir = endPoint - startPoint;
            const f32 gradientLenSq = gradientDir.x * gradientDir.x + gradientDir.y * gradientDir.y;
            if (gradientLenSq < 0.0001f)
                return 0.0f;
            const Float2 toPoint = position - startPoint;
            return (toPoint.x * gradientDir.x + toPoint.y * gradientDir.y) / gradientLenSq;
        }
        [[nodiscard]] Color SampleRamp(f32 t) const override
        {
            return ColorUtils::InterpolateStops(
                Span<const GradientStop>(stops.Data(), stops.Size()), t);
        }

        [[nodiscard]] Color BaseColor() const override
        {
            return stops.IsEmpty() ? Color::White : stops[0].color;
        }
        [[nodiscard]] bool RequiresInterpolation() const override { return true; }
    };

    /// Radial gradient fill from a center point.
    class VGRadialGradientFill final : public IVGFill
    {
    public:
        Float2 center;             ///< Center of the gradient.
        f32 radius = 0.0f;         ///< Radius of the gradient.
        Array<GradientStop> stops; ///< Color stops defining the gradient.

        VGRadialGradientFill() = default;
        VGRadialGradientFill(Float2 inCenter, f32 inRadius) : center(inCenter), radius(inRadius) {}

        void AddStop(f32 offset, Color color) { stops.PushBack(GradientStop(offset, color)); }

        [[nodiscard]] Color GetColorAt(Float2 position, Rectangle bounds) const override
        {
            return SampleRamp(GetParameterAt(position, bounds));
        }
        [[nodiscard]] f32 GetParameterAt(Float2 position, Rectangle /*bounds*/) const override
        {
            if (radius < 0.0001f)
                return 0.0f;
            return Length(position - center) / radius;
        }
        [[nodiscard]] Color SampleRamp(f32 t) const override
        {
            return ColorUtils::InterpolateStops(
                Span<const GradientStop>(stops.Data(), stops.Size()), t);
        }

        [[nodiscard]] Color BaseColor() const override
        {
            return stops.IsEmpty() ? Color::White : stops[0].color;
        }
        [[nodiscard]] bool RequiresInterpolation() const override { return true; }
    };

    /// Conic (angular/sweep) gradient fill around a center point.
    class VGConicGradientFill final : public IVGFill
    {
    public:
        Float2 center;             ///< Center of the gradient.
        f32 startAngle = 0.0f;     ///< Starting angle in radians.
        Array<GradientStop> stops; ///< Color stops defining the gradient.

        VGConicGradientFill() = default;
        explicit VGConicGradientFill(Float2 inCenter, f32 inStartAngle = 0.0f)
            : center(inCenter), startAngle(inStartAngle)
        {
        }

        void AddStop(f32 offset, Color color) { stops.PushBack(GradientStop(offset, color)); }

        [[nodiscard]] Color GetColorAt(Float2 position, Rectangle bounds) const override
        {
            return SampleRamp(GetParameterAt(position, bounds));
        }
        [[nodiscard]] f32 GetParameterAt(Float2 position, Rectangle /*bounds*/) const override
        {
            const f32 dx = position.x - center.x;
            const f32 dy = position.y - center.y;
            f32 angle = Atan2(dy, dx) - startAngle;

            // Normalize to 0..2PI.
            while (angle < 0.0f)
                angle += kTwoPi;
            while (angle >= kTwoPi)
                angle -= kTwoPi;

            return angle / kTwoPi;
        }
        [[nodiscard]] Color SampleRamp(f32 t) const override
        {
            return ColorUtils::InterpolateStops(
                Span<const GradientStop>(stops.Data(), stops.Size()), t);
        }

        [[nodiscard]] Color BaseColor() const override
        {
            return stops.IsEmpty() ? Color::White : stops[0].color;
        }
        [[nodiscard]] bool RequiresInterpolation() const override { return true; }
    };
}
