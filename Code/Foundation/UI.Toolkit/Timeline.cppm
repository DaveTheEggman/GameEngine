// Foundation::UI.Toolkit - the `foundation.ui.toolkit:timeline` partition.
//
// Timeline: a time RULER + draggable PLAYHEAD scrubber over a SECONDS axis
// (property-animation-editor.md D1/D5, Fable ruling 4). Domain-agnostic - it knows a duration, a
// playhead time, and the shared time<->pixel transform (pixelsPerSecond / scrollSeconds /
// labelColumnWidth); it knows NOTHING about clips or keyframes (P2 adds lanes + keys + selection as
// this widget grows, so consumers stay decoupled and it is headlessly testable). SECONDS are the
// only currency (A3); the frame display is a label format only. Scrubbing fires OnPlayheadMoved.
// All chrome resolves from the theme with fallbacks (A5); no hand-picked hex in draw code.

module;
#include <cmath>
#include "Core/Prelude.h"
#include "Core/Reflection/Reflect.h"

export module foundation.ui.toolkit:timeline;

import foundation.core;
import foundation.vg;
import foundation.fonts;
import foundation.ui;

using namespace foundation::core;

export namespace foundation::ui::toolkit
{
    namespace core = foundation::core;
    namespace fonts = foundation::fonts;

    class Timeline : public View
    {
        RTTI_OBJECT(Timeline, View)
    public:
        // The clip length (seconds): the ruler extent + the playhead clamp bound.
        [[nodiscard]] f32 Duration() const noexcept { return m_duration; }
        void SetDuration(f32 seconds)
        {
            m_duration = Max(seconds, 0.0f);
            SetPlayheadTime(m_playhead); // re-clamp to the new length
            Invalidate();
        }

        // Playhead time (seconds), clamped to [0, Duration]. Fires OnPlayheadMoved iff it changed.
        [[nodiscard]] f32 PlayheadTime() const noexcept { return m_playhead; }
        void SetPlayheadTime(f32 seconds)
        {
            const f32 clamped = Clamp(seconds, 0.0f, Max(m_duration, 0.0f));
            if (clamped == m_playhead)
            {
                return;
            }
            m_playhead = clamped;
            OnPlayheadMoved.Invoke(m_playhead);
            Invalidate();
        }

        // The shared time<->pixel transform (D1). P2 lanes + the curve view READ these; none caches
        // its own copy (Traktor's per-row cache was the bug).
        [[nodiscard]] f32 PixelsPerSecond() const noexcept { return m_pixelsPerSecond; }
        void SetPixelsPerSecond(f32 pps)
        {
            m_pixelsPerSecond = Clamp(pps, kMinPps, kMaxPps);
            Invalidate();
        }
        [[nodiscard]] f32 ScrollSeconds() const noexcept { return m_scrollSeconds; }
        void SetScrollSeconds(f32 seconds)
        {
            m_scrollSeconds = Max(seconds, 0.0f);
            Invalidate();
        }

        f32 LabelColumnWidth = 0.0f; // left gutter reserved for track labels (0 in P1)
        i32 DisplayFps = 0;          // 0 = seconds labels; >0 = frame-number labels (display only, A3)

        [[nodiscard]] f32 TimeToX(f32 t) const noexcept
        {
            return LabelColumnWidth + (t - m_scrollSeconds) * m_pixelsPerSecond;
        }
        [[nodiscard]] f32 XToTime(f32 x) const noexcept
        {
            return (m_pixelsPerSecond > 0.0f)
                       ? m_scrollSeconds + (x - LabelColumnWidth) / m_pixelsPerSecond
                       : m_scrollSeconds;
        }

        // Pick the ruler label step (seconds) from the {1,2,5}x10^n sequence: the smallest such step
        // whose on-screen width is at least `minLabelPx`, so labels never collide at any zoom (Godot's
        // ruler algorithm). Pure + headlessly testable (A8).
        [[nodiscard]] static f32 PickTickStep(f32 pixelsPerSecond, f32 minLabelPx) noexcept
        {
            if (pixelsPerSecond <= 0.0f || minLabelPx <= 0.0f)
            {
                return 1.0f;
            }
            const f32 minStep = minLabelPx / pixelsPerSecond; // min seconds between labels
            const f32 mag = std::pow(10.0f, std::floor(std::log10(minStep)));
            const f32 candidates[] = {1.0f * mag, 2.0f * mag, 5.0f * mag, 10.0f * mag};
            for (const f32 c : candidates)
            {
                if (c >= minStep)
                {
                    return c;
                }
            }
            return 10.0f * mag;
        }

        Event<void(f32)> OnPlayheadMoved;

        void OnMeasure(BoxConstraints constraints) override
        {
            MeasuredSize =
                Float2{constraints.ConstrainWidth(200.0f), constraints.ConstrainHeight(kRulerHeight)};
        }

        void OnDraw(UIDrawContext& ctx) override
        {
            const f32 w = Width();
            const f32 h = Height();
            ctx.VG().FillRect(Rectangle{0.0f, 0.0f, w, h},
                              ResolveStyleColor(StyleProperty::Background, core::Color::Rgb(24, 25, 30, 255)));

            const core::Color tickColor =
                ResolveStyleColor(StyleProperty::BorderColor, core::Color::Rgb(62, 64, 74, 255));
            const core::Color labelColor =
                ResolveStyleColor(StyleProperty::TextDimColor, core::Color::Rgb(150, 152, 162, 255));
            fonts::CachedFont* font =
                (ctx.FontService() != nullptr) ? ctx.FontService()->GetFont(9.0f) : nullptr;

            const f32 step = PickTickStep(m_pixelsPerSecond, kMinLabelPx);
            const f32 startTime = Max(XToTime(Max(LabelColumnWidth, 0.0f)), 0.0f);
            const f32 endTime = XToTime(w);
            f32 t = std::floor(startTime / step) * step;
            for (; t <= endTime + step * 0.5f; t += step)
            {
                if (t < -1e-4f)
                {
                    continue;
                }
                const f32 x = TimeToX(t);
                if (x >= LabelColumnWidth && x <= w)
                {
                    ctx.VG().FillRect(Rectangle{x, h * 0.45f, 1.0f, h * 0.55f}, tickColor);
                    if (font != nullptr)
                    {
                        const String label = FormatTime(t);
                        ctx.VG().DrawText(label, font, Rectangle{x + 3.0f, 1.0f, 46.0f, 12.0f},
                                          fonts::TextAlignment::Left, fonts::VerticalAlignment::Top,
                                          labelColor);
                    }
                }
                // A minor tick halfway to the next label.
                const f32 xm = TimeToX(t + step * 0.5f);
                if (xm >= LabelColumnWidth && xm <= w)
                {
                    ctx.VG().FillRect(Rectangle{xm, h * 0.72f, 1.0f, h * 0.28f}, tickColor);
                }
            }

            // Left label gutter (P1: empty; the rows below label themselves).
            if (LabelColumnWidth > 0.0f)
            {
                ctx.VG().FillRect(Rectangle{0.0f, 0.0f, LabelColumnWidth, h},
                                  ResolveStyleColor(StyleProperty::Background, core::Color::Rgb(20, 21, 26, 255)));
            }

            // Playhead: a vertical line + a small head flag at the top (theme error-red).
            const f32 px = TimeToX(m_playhead);
            if (px >= LabelColumnWidth - 0.5f && px <= w)
            {
                const core::Color playhead =
                    ResolveStyleColor(StyleProperty::ErrorColor, core::Color::Rgb(232, 84, 84, 255));
                ctx.VG().FillRect(Rectangle{px - 0.5f, 0.0f, 1.5f, h}, playhead);
                ctx.VG().FillRect(Rectangle{px - 4.0f, 0.0f, 8.0f, 5.0f}, playhead);
            }
        }

        void OnMouseDown(MouseEventArgs& e) override
        {
            if (e.Button != MouseButton::Left || e.X < LabelColumnWidth)
            {
                return;
            }
            m_dragging = true;
            SetPlayheadTime(XToTime(e.X));
            if (Context != nullptr)
            {
                Context->GetFocusManager()->SetCapture(this);
            }
            e.Handled = true;
        }

        void OnMouseMove(MouseEventArgs& e) override
        {
            if (!m_dragging)
            {
                return;
            }
            SetPlayheadTime(XToTime(e.X));
            e.Handled = true;
        }

        void OnMouseUp(MouseEventArgs& e) override
        {
            if (e.Button != MouseButton::Left || !m_dragging)
            {
                return;
            }
            m_dragging = false;
            if (Context != nullptr)
            {
                Context->GetFocusManager()->ReleaseCapture();
            }
            e.Handled = true;
        }

        void OnMouseWheel(MouseWheelEventArgs& e) override
        {
            if (e.DeltaY == 0.0f)
            {
                return;
            }
            // Zoom anchored at the cursor: keep the time under the cursor fixed (Godot).
            const f32 timeAtCursor = XToTime(e.X);
            SetPixelsPerSecond(m_pixelsPerSecond * (e.DeltaY > 0.0f ? kZoomStep : 1.0f / kZoomStep));
            m_scrollSeconds = Max(timeAtCursor - (e.X - LabelColumnWidth) / m_pixelsPerSecond, 0.0f);
            e.Handled = true;
            Invalidate();
        }

    private:
        [[nodiscard]] String FormatTime(f32 t) const
        {
            String s;
            if (DisplayFps > 0)
            {
                AppendValue(s, static_cast<i32>(std::lround(t * static_cast<f32>(DisplayFps))));
                s += u8"f";
            }
            else
            {
                AppendValue(s, t);
                s += u8"s";
            }
            return s;
        }

        static constexpr f32 kRulerHeight = 24.0f;
        static constexpr f32 kMinPps = 4.0f;
        static constexpr f32 kMaxPps = 4000.0f;
        static constexpr f32 kMinLabelPx = 48.0f;
        static constexpr f32 kZoomStep = 1.15f;

        f32 m_duration = 1.0f;
        f32 m_playhead = 0.0f;
        f32 m_pixelsPerSecond = 100.0f;
        f32 m_scrollSeconds = 0.0f;
        bool m_dragging = false;
    };

    RTTI_DEFINE_OBJECT(Timeline, "rtti::ui::toolkit")
}
