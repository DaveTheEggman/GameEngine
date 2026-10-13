// SPDX-License-Identifier: MIT
// Copyright (c) 2026-Present Robert Campbell

// Vulkan backend: what the profiler needs from queries (Specs/profiler.md, P1). A queue reads its
// timestamp and core::GetTicks' clock together (calibrated timestamps), and a pipeline-statistics
// query resolves to a record as wide as the device says, decoded into the RHI's counters. The
// cases run on a real adapter and skip without one.
#include <doctest/doctest.h>
#include "Core/Prelude.h"
#include "RHI.Vulkan/VkIncludes.h"

import foundation.core;
import foundation.rhi;
import foundation.rhi.vulkan;

using namespace foundation::core;
using namespace foundation::rhi;

namespace
{
    Device* MakeDevice(Backend*& outBackend)
    {
        outBackend = nullptr;
        (void)vk::CreateBackend(vk::VkBackendDesc{}, outBackend);
        if (outBackend == nullptr || outBackend->EnumerateAdapters().IsEmpty())
        {
            return nullptr;
        }
        Device* device = nullptr;
        if (!outBackend->EnumerateAdapters()[0]->CreateDevice(DeviceDesc{}, device).IsOk())
        {
            return nullptr;
        }
        return device;
    }

    void Wait(u64 milliseconds)
    {
        const u64 until = GetTicks() + milliseconds * GetTickFrequency() / 1000u;
        while (GetTicks() < until)
        {
        }
    }
}

TEST_CASE("vk.queries: a calibration reads the GPU's timestamp beside core::GetTicks")
{
    Backend* backend = nullptr;
    Device* device = MakeDevice(backend);
    if (device == nullptr)
    {
        MESSAGE("no Vulkan adapter - skipped");
        return;
    }
    Queue* queue = device->GetQueue(QueueType::Graphics);
    REQUIRE(queue != nullptr);
    if (!device->features.timestampCalibration)
    {
        u64 gpu = 7, cpu = 9;
        CHECK_FALSE(queue->CalibrateTimestamps(gpu, cpu));
        CHECK(gpu == 7u); // left alone
        MESSAGE("no calibrated timestamps on this adapter - only the refusal checked");
        device->Destroy();
        return;
    }

    const u64 before = GetTicks();
    u64 gpu0 = 0, cpu0 = 0;
    REQUIRE(queue->CalibrateTimestamps(gpu0, cpu0));
    const u64 after = GetTicks();
    // The CPU side is core::GetTicks' own clock: it lies between two reads of it.
    CHECK(cpu0 >= before);
    CHECK(cpu0 <= after);

    // Over a while, the GPU's ticks times its period track the CPU's time.
    Wait(30);
    u64 gpu1 = 0, cpu1 = 0;
    REQUIRE(queue->CalibrateTimestamps(gpu1, cpu1));
    const f64 cpuMs = TicksToMilliseconds(cpu1 - cpu0);
    const f64 gpuMs = static_cast<f64>(gpu1 - gpu0) * queue->TimestampPeriod() / 1.0e6;
    MESSAGE("calibration: CPU ", cpuMs, " ms, GPU ", gpuMs, " ms");
    CHECK(cpuMs >= 29.0);
    CHECK(gpuMs == doctest::Approx(cpuMs).epsilon(0.01));
    device->Destroy();
}

TEST_CASE("vk.queries: a statistics query resolves to a record the device decodes")
{
    Backend* backend = nullptr;
    Device* device = MakeDevice(backend);
    if (device == nullptr)
    {
        MESSAGE("no Vulkan adapter - skipped");
        return;
    }
    CHECK(device->QueryResultSize(QueryType::Timestamp) == 8u);
    if (!device->features.pipelineStatisticsQueries)
    {
        CHECK(device->QueryResultSize(QueryType::PipelineStatistics) == 0u);
        MESSAGE("no pipeline statistics on this adapter - skipped");
        device->Destroy();
        return;
    }
    const u32 recordSize = device->QueryResultSize(QueryType::PipelineStatistics);
    REQUIRE(recordSize >= sizeof(u64) * 6u);

    QuerySetDesc qd{};
    qd.type = QueryType::PipelineStatistics;
    qd.count = 2;
    qd.label = u8"vk.queries.statistics";
    QuerySet* set = nullptr;
    REQUIRE(device->CreateQuerySet(qd, set).IsOk());
    BufferDesc bd{};
    bd.size = static_cast<u64>(recordSize) * 2u;
    bd.usage = BufferUsage::CopyDst;
    bd.memory = MemoryLocation::GpuToCpu;
    bd.label = u8"vk.queries.readback";
    Buffer* readback = nullptr;
    REQUIRE(device->CreateBuffer(bd, readback).IsOk());
    // Fill the readback with a pattern, so a resolve that writes nothing would show.
    if (u8* bytes = static_cast<u8*>(readback->Map()))
    {
        for (u64 i = 0; i < bd.size; ++i)
        {
            bytes[i] = 0xAB;
        }
        readback->Unmap();
    }

    CommandPool* pool = nullptr;
    REQUIRE(device->CreateCommandPool(QueueType::Graphics, pool).IsOk());
    CommandEncoder* enc = nullptr;
    REQUIRE(pool->CreateEncoder(enc).IsOk());
    enc->ResetQuerySet(set, 0, 2);
    // Two queries around no work at all: every counter reads zero, each in its own record.
    enc->BeginPipelineStatisticsQuery(set, 0);
    enc->EndPipelineStatisticsQuery(set, 0);
    enc->BeginPipelineStatisticsQuery(set, 1);
    enc->EndPipelineStatisticsQuery(set, 1);
    enc->ResolveQuerySet(set, 0, 2, readback, 0);
    CommandBuffer* cb = enc->Finish();
    REQUIRE(cb != nullptr);
    Fence* fence = nullptr;
    REQUIRE(device->CreateFence(0, fence).IsOk());
    Queue* queue = device->GetQueue(QueueType::Graphics);
    CommandBuffer* cbs[] = {cb};
    queue->Submit(Span<CommandBuffer* const>(cbs, 1), fence, 1);
    REQUIRE(fence->Wait(1, ~0ull));

    const u8* bytes = static_cast<const u8*>(readback->Map());
    REQUIRE(bytes != nullptr);
    for (u32 q = 0; q < 2; ++q)
    {
        const PipelineStatistics stats = device->DecodePipelineStatistics(bytes + q * recordSize);
        CHECK(stats.inputVertices == 0u);
        CHECK(stats.inputPrimitives == 0u);
        CHECK(stats.vertexInvocations == 0u);
        CHECK(stats.clippingPrimitives == 0u);
        CHECK(stats.fragmentInvocations == 0u);
        CHECK(stats.computeInvocations == 0u);
    }
    readback->Unmap();

    pool->DestroyEncoder(enc);
    device->DestroyFence(fence);
    device->DestroyCommandPool(pool);
    device->DestroyBuffer(readback);
    device->DestroyQuerySet(set);
    device->Destroy();
}
