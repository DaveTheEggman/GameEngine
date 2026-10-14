// SPDX-License-Identifier: MIT
// Copyright (c) 2026-Present Robert Campbell

/// Foundation::Profiler.UI - :frame_time_graph partition.
///
/// The frame times of the newest frames as lines, newest at the right: the CPU's over a filled
/// area, the GPU's on top, and the 60 and 30 fps budgets (16.7 and 33.3 ms) as faint lines where
/// they fit. The scale steps up in doublings from 20 ms so a hitch neither flattens the rest for
/// long nor makes the graph jitter frame to frame.

module;
#include "Core/Prelude.h"

export module foundation.profiler.ui:frame_time_graph;

import foundation.core;
import foundation.vg;
import foundation.fonts;
import foundation.ui;
import foundation.profiler;
import :window;
import :view;

using namespace foundation::core;
namespace fonts = foundation::fonts;

export namespace foundation::profiler::ui
{
    class FrameTimeGraph final : public ProfilerView
    {
    public:
        static constexpr f32 kHeight = 64.0f;
        static constexpr f32 kBudgets[] = {16.667f, 33.333f};

        explicit FrameTimeGraph(IAllocator& allocator)
            : ProfilerView(allocator), m_times(allocator), m_cpuPoints(allocator), m_gpuPoints(allocator)
        {
        }

        /// The model: the frames shown, the graph's top in ms, and the lines' points in the view.
        [[nodiscard]] Span<const FrameTime> Times() const noexcept { return m_times.AsSpan(); }
        [[nodiscard]] f32 ScaleMs() const noexcept { return m_scaleMs; }
        [[nodiscard]] Span<const Float2> CpuPoints() const noexcept { return m_cpuPoints.AsSpan(); }
        [[nodiscard]] Span<const Float2> GpuPoints() const noexcept { return m_gpuPoints.AsSpan(); }

        /// The scale for a highest time: 20 ms doubled until it holds it.
        [[nodiscard]] static f32 ScaleFor(f32 highestMs) noexcept
        {
            f32 scale = 20.0f;
            while (scale < highestMs && scale < 10240.0f)
            {
                scale *= 2.0f;
            }
            return scale;
        }

        void OnDraw(foundation::ui::UIDrawContext& ctx) override
        {
            Compute(); // the size may have changed since the last refresh
            const f32 w = Width();
            const f32 h = Height();
            ctx.VG().FillRect(Rectangle{0, 0, w, h}, palette.plot);
            for (const f32 budget : kBudgets)
            {
                if (budget < m_scaleMs)
                {
                    const f32 y = YOf(budget);
                    ctx.VG().DrawLine(Float2{0, y}, Float2{w, y}, palette.budget, 1.0f);
                    DrawLabel(ctx, FormatFixed(budget, 1, Allocator()).AsView(), Rectangle{2, y - kLineHeight, 60, kLineHeight},
                              palette.dim);
                }
            }
            if (m_cpuPoints.Size() >= 2)
            {
                // The area under the CPU line, then the line.
                ctx.VG().BeginPath();
                ctx.VG().MoveTo(Float2{m_cpuPoints[0].x, h});
                for (const Float2 p : m_cpuPoints)
                {
                    ctx.VG().LineTo(p);
                }
                ctx.VG().LineTo(Float2{m_cpuPoints[m_cpuPoints.Size() - 1].x, h});
                ctx.VG().ClosePath();
                ctx.VG().Fill(Color{palette.cpu.r, palette.cpu.g, palette.cpu.b, 0.22f});
                StrokeLine(ctx, m_cpuPoints.AsSpan(), palette.cpu);
            }
            if (m_gpuPoints.Size() >= 2)
            {
                StrokeLine(ctx, m_gpuPoints.AsSpan(), palette.gpu);
            }
            DrawLabel(ctx, FormatFixed(m_scaleMs, 0, Allocator()).AsView(), Rectangle{w - 62, 0, 60, kLineHeight}, palette.dim,
                      fonts::TextAlignment::Right);
        }

    protected:
        void OnMeasure(foundation::ui::BoxConstraints constraints) override
        {
            MeasuredSize = Float2{constraints.ConstrainWidth(300.0f), constraints.ConstrainHeight(kHeight)};
        }

        void Compute() override
        {
            CollectFrameTimes(SourceOrEmpty(), Range(), m_times);
            f32 highest = 0.0f;
            for (const FrameTime& t : m_times)
            {
                highest = Max(highest, Max(t.cpuMs, t.hasGpu ? t.gpuMs : 0.0f));
            }
            m_scaleMs = ScaleFor(highest);
            // Slots for the whole window, newest at the right edge, so a short history grows in
            // from the right rather than stretching.
            m_cpuPoints.Clear();
            m_gpuPoints.Clear();
            const f32 w = Width();
            const f32 step = m_frameCount > 1 ? w / static_cast<f32>(m_frameCount - 1) : w;
            const u64 newest = m_times.IsEmpty() ? 0 : m_times[m_times.Size() - 1].frame;
            for (const FrameTime& t : m_times)
            {
                const f32 x = w - static_cast<f32>(newest - t.frame) * step;
                m_cpuPoints.PushBack(Float2{x, YOf(t.cpuMs)});
                if (t.hasGpu)
                {
                    m_gpuPoints.PushBack(Float2{x, YOf(t.gpuMs)});
                }
            }
        }

    private:
        [[nodiscard]] f32 YOf(f32 ms) const noexcept
        {
            return Height() * (1.0f - Min(ms / m_scaleMs, 1.0f));
        }

        static void StrokeLine(foundation::ui::UIDrawContext& ctx, Span<const Float2> points, Color color)
        {
            ctx.VG().BeginPath();
            ctx.VG().MoveTo(points[0]);
            for (usize i = 1; i < points.Size(); ++i)
            {
                ctx.VG().LineTo(points[i]);
            }
            ctx.VG().Stroke(color, 1.5f);
        }

        Array<FrameTime> m_times;
        Array<Float2> m_cpuPoints;
        Array<Float2> m_gpuPoints;
        f32 m_scaleMs = 20.0f;
    };
}
