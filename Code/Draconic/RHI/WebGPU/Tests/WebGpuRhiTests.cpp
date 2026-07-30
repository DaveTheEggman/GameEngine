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

    // Buffers are implemented now - an EMPTY desc must fail validation, not
    // hand back wgpu's "invalid object".
    Buffer* buffer = nullptr;
    CHECK(device->CreateBuffer(BufferDesc{}, buffer).Code() == ErrorCode::InvalidArgument);
    CHECK(buffer == nullptr);

    // Encoders are the next stage - still an honest NotSupported.
    CommandPool* pool = nullptr;
    CHECK(device->CreateCommandPool(QueueType::Graphics, pool).Code() ==
          ErrorCode::NotSupported);

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

TEST_CASE("rhi.webgpu: resources - buffer map emulation, texture + view, sampler, WGSL shader")
{
    Backend* backend = TryCreateBackend();
    if (backend == nullptr)
    {
        return;
    }
    Device* device = nullptr;
    REQUIRE(backend->EnumerateAdapters()[0]->CreateDevice(DeviceDesc{}, device).IsOk());

    // CpuToGpu buffer: Map hands out the CPU shadow, Unmap flushes via WriteBuffer.
    BufferDesc bufferDesc;
    bufferDesc.size = 256;
    bufferDesc.usage = BufferUsage::Uniform | BufferUsage::CopyDst;
    bufferDesc.memory = MemoryLocation::CpuToGpu;
    Buffer* buffer = nullptr;
    REQUIRE(device->CreateBuffer(bufferDesc, buffer).IsOk());
    void* mapped = buffer->Map();
    REQUIRE(mapped != nullptr);
    MemSet(mapped, 0xAB, 256);
    buffer->Unmap(); // queue-ordered upload; GPU validation would log on error
    device->WaitIdle();
    CHECK(!device->IsLost());

    // GpuOnly: no host view, by contract.
    BufferDesc gpuOnly;
    gpuOnly.size = 64;
    gpuOnly.usage = BufferUsage::Storage;
    gpuOnly.memory = MemoryLocation::GpuOnly;
    Buffer* deviceLocal = nullptr;
    REQUIRE(device->CreateBuffer(gpuOnly, deviceLocal).IsOk());
    CHECK(deviceLocal->Map() == nullptr);

    // Texture + a full-resource view.
    Texture* texture = nullptr;
    REQUIRE(device
                ->CreateTexture(TextureDesc::RenderTarget(TextureFormat::RGBA8Unorm, 64, 64),
                                texture)
                .IsOk());
    TextureViewDesc viewDesc;
    viewDesc.format = TextureFormat::RGBA8Unorm;
    TextureView* view = nullptr;
    REQUIRE(device->CreateTextureView(texture, viewDesc, view).IsOk());
    CHECK(view->texture == texture);

    // A format WebGPU does not have fails honestly.
    Texture* unsupported = nullptr;
    CHECK(device
              ->CreateTexture(TextureDesc::RenderTarget(TextureFormat::RGBA16Unorm, 4, 4),
                              unsupported)
              .Code() == ErrorCode::NotSupported);

    // Sampler, including the anisotropy-requires-linear clamp.
    SamplerDesc samplerDesc;
    samplerDesc.minFilter = FilterMode::Nearest;
    samplerDesc.maxAnisotropy = 8; // invalid with nearest - backend clamps to 1
    Sampler* sampler = nullptr;
    REQUIRE(device->CreateSampler(samplerDesc, sampler).IsOk());

    // WGSL shader module (the non-SPIR-V ingestion branch; SPIR-V rides the samples).
    const char8_t* wgsl =
        u8"@vertex fn main() -> @builtin(position) vec4f { return vec4f(0.0); }";
    ShaderModuleDesc moduleDesc;
    moduleDesc.code = Span<const u8>(reinterpret_cast<const u8*>(wgsl),
                                     StringView(wgsl).Size());
    ShaderModule* shaderModule = nullptr;
    REQUIRE(device->CreateShaderModule(moduleDesc, shaderModule).IsOk());

    device->DestroyShaderModule(shaderModule);
    device->DestroySampler(sampler);
    device->DestroyTextureView(view);
    device->DestroyTexture(texture);
    device->DestroyBuffer(deviceLocal);
    device->DestroyBuffer(buffer);
    device->WaitIdle();
    CHECK(!device->IsLost());
    device->Destroy();
    backend->Destroy();
}
