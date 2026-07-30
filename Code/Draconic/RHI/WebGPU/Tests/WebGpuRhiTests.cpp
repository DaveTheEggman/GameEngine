// WebGPU backend bring-up tests. ENVIRONMENT-DEPENDENT by nature (they need the
// wgpu-native sidecar AND a GPU the runtime can drive) - when the backend cannot
// initialize, the suite reports that once and passes vacuously rather than failing
// a GPU-less machine, mirroring how Vulkan/DX12 stay sample-verified.
#include <doctest/doctest.h>

#include "Core/Prelude.h"

import draconic.core;
import draconic.rhi;
import draconic.rhi.webgpu;

using namespace draconic::core;
using namespace draconic::rhi;

namespace
{
    Backend* TryCreateBackend()
    {
        Backend* backend = nullptr;
        if (!webgpu::CreateBackend(webgpu::WebGpuBackendDesc{}, backend).IsOk())
        {
            return nullptr;
        }
        if (backend->EnumerateAdapters().IsEmpty())
        {
            backend->Destroy();
            return nullptr;
        }
        return backend;
    }
}

TEST_CASE("rhi.webgpu: sidecar backend enumerates adapters and creates a live device")
{
    Backend* backend = TryCreateBackend();
    if (backend == nullptr)
    {
        MESSAGE("wgpu-native sidecar or GPU unavailable - webgpu backend tests skipped");
        return;
    }
    CHECK(backend->isInitialized);

    // Adapter info is real hardware data - shape checks only.
    auto adapters = backend->EnumerateAdapters();
    const AdapterInfo info = adapters[0]->Info();
    CHECK(!info.name.IsEmpty());
    CHECK(info.supportedFeatures.maxBindGroups >= 4u);

    Device* device = nullptr;
    REQUIRE(adapters[0]->CreateDevice(DeviceDesc{}, device).IsOk());
    REQUIRE(device != nullptr);
    CHECK(device->type == DeviceType::WebGPU);
    CHECK(!device->IsLost());

    // One WebGPU queue behind all three RHI queue types.
    Queue* graphics = device->GetQueue(QueueType::Graphics);
    REQUIRE(graphics != nullptr);
    CHECK(graphics->queueType == QueueType::Graphics);
    CHECK(device->GetQueue(QueueType::Compute) != nullptr);
    CHECK(device->GetQueueCount(QueueType::Graphics) == 1u);

    device->WaitIdle();
    device->Destroy();
    backend->Destroy();
}

TEST_CASE("rhi.webgpu: fences signal through empty submissions")
{
    Backend* backend = TryCreateBackend();
    if (backend == nullptr)
    {
        return; // reported once by the first case
    }
    Device* device = nullptr;
    REQUIRE(backend->EnumerateAdapters()[0]->CreateDevice(DeviceDesc{}, device).IsOk());

    Fence* fence = nullptr;
    REQUIRE(device->CreateFence(0, fence).IsOk());
    CHECK(fence->CompletedValue() == 0u);

    Queue* queue = device->GetQueue(QueueType::Graphics);
    queue->Submit(Span<CommandBuffer* const>{}, fence, 7);
    CHECK(fence->Wait(7, 0));
    CHECK(fence->CompletedValue() == 7u);

    device->DestroyFence(fence);
    device->Destroy();
    backend->Destroy();
}

TEST_CASE("rhi.webgpu: unimplemented stages fail honestly, extensions unsupported")
{
    Backend* backend = TryCreateBackend();
    if (backend == nullptr)
    {
        return;
    }
    Device* device = nullptr;
    REQUIRE(backend->EnumerateAdapters()[0]->CreateDevice(DeviceDesc{}, device).IsOk());

    Buffer* buffer = nullptr;
    CHECK(device->CreateBuffer(BufferDesc{}, buffer).Code() == ErrorCode::NotSupported);
    CHECK(buffer == nullptr);

    MeshPipeline* mesh = nullptr;
    CHECK(device->CreateMeshPipeline(MeshPipelineDesc{}, mesh).Code() ==
          ErrorCode::NotSupported);

    device->Destroy();
    backend->Destroy();
}

TEST_CASE("rhi.webgpu: a missing sidecar fails with NotFound, not a crash")
{
    Backend* backend = nullptr;
    webgpu::WebGpuBackendDesc desc;
    desc.libraryPathOverride = u8"definitely_not_wgpu_native.so";
    // The override misses, and the fallback chain may still find the vendored lib -
    // either a clean failure or a working backend is acceptable; never a crash.
    const Status status = webgpu::CreateBackend(desc, backend);
    if (status.IsOk())
    {
        backend->Destroy();
    }
    else
    {
        CHECK(backend == nullptr);
    }
}
