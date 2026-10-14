// SPDX-License-Identifier: MIT
// Copyright (c) 2026-Present Robert Campbell

/// Foundation::Profiler.UI - :window partition.
///
/// What the views show, computed from a source and nothing drawn: the frame times of the last
/// frames, their statistics, the scopes or GPU passes that took longest over them, and the
/// counters' values. Views call these as they draw; tests call them on a source of known frames.

module;
#include "Core/Prelude.h"

export module foundation.profiler.ui:window;

import foundation.core;
import foundation.profiler;

using namespace foundation::core;

export namespace foundation::profiler::ui
{
    /// One frame's times. The GPU time is the span of its passes, from the first to start to the
    /// last to end; it is missing for the newest frames, whose passes have not arrived yet.
    struct FrameTime
    {
        u64 frame = 0;
        f32 cpuMs = 0.0f;
        f32 gpuMs = 0.0f;
        bool hasGpu = false;
    };

    /// Min, median, max and mean of a set of times; `count` 0 when there were none.
    struct TimeStats
    {
        f32 min = 0.0f;
        f32 median = 0.0f;
        f32 max = 0.0f;
        f32 mean = 0.0f;
        u32 count = 0;
    };

    /// The frames a view looks at: the last `count` frame numbers up to the source's newest.
    struct FrameRange
    {
        u64 first = 0;
        u64 last = 0;
        bool empty = true;
    };

    [[nodiscard]] inline FrameRange LastFrames(const IProfileSource& source, u32 count) noexcept
    {
        FrameRange range;
        if (!source.HasFrames() || count == 0)
        {
            return range;
        }
        range.last = source.NewestFrame();
        const u64 oldest = source.OldestFrame();
        range.first = range.last - oldest + 1 > count ? range.last + 1 - count : oldest;
        range.empty = false;
        return range;
    }

    [[nodiscard]] inline f32 TicksToMs(u64 ticks, u64 tickFrequency) noexcept
    {
        return static_cast<f32>(static_cast<f64>(ticks) * 1000.0 / static_cast<f64>(Max<u64>(tickFrequency, 1)));
    }

    /// The times of the frames in `range` that have a record, oldest first.
    inline void CollectFrameTimes(const IProfileSource& source, FrameRange range, Array<FrameTime>& out)
    {
        out.Clear();
        if (range.empty)
        {
            return;
        }
        const u64 frequency = source.TickFrequency();
        for (u64 n = range.first; n <= range.last; ++n)
        {
            const FrameRecord* frame = source.Frame(n);
            if (frame == nullptr)
            {
                continue;
            }
            FrameTime time;
            time.frame = n;
            time.cpuMs = TicksToMs(frame->endTick - frame->beginTick, frequency);
            if (frame->gpuValid && !frame->gpu.IsEmpty())
            {
                u64 begin = frame->gpu[0].beginTick;
                u64 end = frame->gpu[0].endTick;
                for (const GpuPassEvent& pass : frame->gpu)
                {
                    begin = Min(begin, pass.beginTick);
                    end = Max(end, pass.endTick);
                }
                time.gpuMs = TicksToMs(end - begin, frequency);
                time.hasGpu = true;
            }
            out.PushBack(time);
        }
    }

    /// The statistics of `values` (sorted in place).
    inline TimeStats StatsOf(Array<f32>& values)
    {
        TimeStats stats;
        if (values.IsEmpty())
        {
            return stats;
        }
        values.Sort([](f32 a, f32 b) { return a < b; });
        f64 sum = 0.0;
        for (const f32 v : values)
        {
            sum += v;
        }
        stats.min = values[0];
        stats.median = values[values.Size() / 2];
        stats.max = values[values.Size() - 1];
        stats.mean = static_cast<f32>(sum / static_cast<f64>(values.Size()));
        stats.count = static_cast<u32>(values.Size());
        return stats;
    }

    /// CPU and GPU statistics over a set of frame times (the GPU's over the frames that have it).
    struct FrameWindowStats
    {
        TimeStats cpu;
        TimeStats gpu;
    };

    [[nodiscard]] inline FrameWindowStats StatsOf(Span<const FrameTime> times, IAllocator& allocator)
    {
        Array<f32> cpu(allocator);
        Array<f32> gpu(allocator);
        for (const FrameTime& t : times)
        {
            cpu.PushBack(t.cpuMs);
            if (t.hasGpu)
            {
                gpu.PushBack(t.gpuMs);
            }
        }
        FrameWindowStats stats;
        stats.cpu = StatsOf(cpu);
        stats.gpu = StatsOf(gpu);
        return stats;
    }

    /// Which events a top list ranks.
    enum class TopLane : u8
    {
        Scopes,    // CPU scopes on every thread, by their own time (their children's taken out)
        GpuPasses, // GPU passes, by their time
    };

    /// A name's time over a range of frames: `msPerFrame` is its total over the frames with a
    /// record, averaged per frame; `calls` its count, likewise.
    struct TopEntry
    {
        NameId name = 0;
        f32 msPerFrame = 0.0f;
        f32 callsPerFrame = 0.0f;
    };

    /// The `count` names that took longest over `range`, longest first.
    inline void CollectTop(const IProfileSource& source, FrameRange range, TopLane lane, u32 count,
                           Array<TopEntry>& out)
    {
        out.Clear();
        if (range.empty)
        {
            return;
        }
        IAllocator& allocator = out.Allocator();
        Array<u64> ticks(allocator);  // by name id
        Array<u64> calls(allocator);
        Array<u64> childTicks(allocator); // per depth: the time the scopes ended below it took
        u32 frames = 0;
        auto add = [&](NameId name, u64 duration)
        {
            if (name >= ticks.Size())
            {
                ticks.Resize(name + 1, 0);
                calls.Resize(name + 1, 0);
            }
            ticks[name] += duration;
            ++calls[name];
        };
        for (u64 n = range.first; n <= range.last; ++n)
        {
            const FrameRecord* frame = source.Frame(n);
            if (frame == nullptr || (lane == TopLane::GpuPasses && !frame->gpuValid))
            {
                continue;
            }
            ++frames;
            if (lane == TopLane::GpuPasses)
            {
                for (const GpuPassEvent& pass : frame->gpu)
                {
                    add(pass.name, pass.endTick - pass.beginTick);
                }
                continue;
            }
            // A thread's scopes are in the order they ended, so a scope's children come before
            // it: what ended one level deeper since the last scope at its depth is its children.
            for (const ThreadFrame& thread : frame->threads)
            {
                childTicks.Clear();
                for (const ScopeEvent& scope : thread.scopes)
                {
                    if (childTicks.Size() < scope.depth + 2)
                    {
                        childTicks.Resize(scope.depth + 2, 0);
                    }
                    const u64 duration = scope.endTick - scope.beginTick;
                    const u64 children = Min(childTicks[scope.depth + 1], duration);
                    childTicks[scope.depth + 1] = 0;
                    childTicks[scope.depth] += duration;
                    add(scope.name, duration - children);
                }
            }
        }
        if (frames == 0)
        {
            return;
        }
        const u64 frequency = source.TickFrequency();
        for (usize id = 0; id < ticks.Size(); ++id)
        {
            if (calls[id] == 0)
            {
                continue;
            }
            TopEntry entry;
            entry.name = static_cast<NameId>(id);
            entry.msPerFrame = TicksToMs(ticks[id], frequency) / static_cast<f32>(frames);
            entry.callsPerFrame = static_cast<f32>(calls[id]) / static_cast<f32>(frames);
            // Insert in order, longest first, keeping `count`.
            usize at = out.Size();
            while (at > 0 && out[at - 1].msPerFrame < entry.msPerFrame)
            {
                --at;
            }
            if (at >= count)
            {
                continue;
            }
            out.Insert(at, entry);
            if (out.Size() > count)
            {
                out.PopBack();
            }
        }
    }

    /// A counter's value in the newest frame that set it, and its mean over the range.
    struct CounterReading
    {
        u32 counter = 0;
        f64 latest = 0.0;
        f64 mean = 0.0;
        bool present = false;
    };

    /// One reading per registered counter, in registration order.
    inline void CollectCounters(const IProfileSource& source, FrameRange range, Array<CounterReading>& out)
    {
        out.Clear();
        const usize counters = source.Counters().Size();
        out.Resize(counters, CounterReading{});
        if (range.empty)
        {
            return;
        }
        IAllocator& allocator = out.Allocator();
        Array<f64> sums(allocator);
        Array<u32> seen(allocator);
        sums.Resize(counters, 0.0);
        seen.Resize(counters, 0);
        for (u64 n = range.first; n <= range.last; ++n)
        {
            const FrameRecord* frame = source.Frame(n);
            if (frame == nullptr)
            {
                continue;
            }
            for (const CounterValue& value : frame->counters)
            {
                if (value.counter < counters)
                {
                    sums[value.counter] += value.value;
                    ++seen[value.counter];
                    out[value.counter].latest = value.value; // frames run oldest first
                }
            }
        }
        for (usize i = 0; i < counters; ++i)
        {
            out[i].counter = static_cast<u32>(i);
            out[i].present = seen[i] > 0;
            out[i].mean = seen[i] > 0 ? sums[i] / static_cast<f64>(seen[i]) : 0.0;
        }
    }

    /// A counter's value as text in its unit: a count whole, bytes in KiB, MiB or GiB, times in ms,
    /// a percentage with its sign.
    [[nodiscard]] inline String FormatCounter(f64 value, CounterUnit unit, IAllocator& allocator)
    {
        constexpr f64 kKiB = 1024.0;
        String out(allocator);
        switch (unit)
        {
        case CounterUnit::Bytes:
            if (value >= kKiB * kKiB * kKiB)
            {
                AppendFormat(out, u8"{} GiB", FormatFixed(value / (kKiB * kKiB * kKiB), 2, allocator).AsView());
            }
            else if (value >= kKiB * kKiB)
            {
                AppendFormat(out, u8"{} MiB", FormatFixed(value / (kKiB * kKiB), 1, allocator).AsView());
            }
            else
            {
                AppendFormat(out, u8"{} KiB", FormatFixed(value / kKiB, 1, allocator).AsView());
            }
            break;
        case CounterUnit::Milliseconds:
            AppendFormat(out, u8"{} ms", FormatFixed(value, 2, allocator).AsView());
            break;
        case CounterUnit::Percent:
            AppendFormat(out, u8"{}%", FormatFixed(value, 1, allocator).AsView());
            break;
        case CounterUnit::Count:
        default:
            AppendFormat(out, u8"{}", static_cast<i64>(value + (value < 0.0 ? -0.5 : 0.5)));
            break;
        }
        return out;
    }
}
