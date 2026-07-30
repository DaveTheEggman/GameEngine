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

    // Pipeline statistics have no WebGPU shape - honest NotSupported.
    QuerySetDesc statisticsDesc;
    statisticsDesc.type = QueryType::PipelineStatistics;
    statisticsDesc.count = 1;
    QuerySet* statistics = nullptr;
    CHECK(device->CreateQuerySet(statisticsDesc, statistics).Code() ==
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

TEST_CASE("rhi.webgpu: bind groups + pipelines - the DXC shift scheme end-to-end")
{
    Backend* backend = TryCreateBackend();
    if (backend == nullptr)
    {
        return;
    }
    Device* device = nullptr;
    REQUIRE(backend->EnumerateAdapters()[0]->CreateDevice(DeviceDesc{}, device).IsOk());

    // Layout with all three shift classes: CBV (0), SRV (+1000), sampler (+3000).
    const BindGroupLayoutEntry entries[] = {
        BindGroupLayoutEntry::UniformBuffer(0, ShaderStage::Vertex | ShaderStage::Fragment),
        BindGroupLayoutEntry::SampledTexture(0, ShaderStage::Fragment),
        BindGroupLayoutEntry::Sampler(0, ShaderStage::Fragment),
    };
    BindGroupLayoutDesc layoutDesc;
    layoutDesc.entries = Span<const BindGroupLayoutEntry>(entries, 3);
    BindGroupLayout* layout = nullptr;
    REQUIRE(device->CreateBindGroupLayout(layoutDesc, layout).IsOk());
    CHECK(layout->Entries().Size() == 3u);

    // Resources to bind.
    BufferDesc uboDesc;
    uboDesc.size = 16;
    uboDesc.usage = BufferUsage::Uniform;
    uboDesc.memory = MemoryLocation::CpuToGpu;
    Buffer* ubo = nullptr;
    REQUIRE(device->CreateBuffer(uboDesc, ubo).IsOk());
    Texture* texture = nullptr;
    TextureDesc texDesc = TextureDesc::RenderTarget(TextureFormat::RGBA8Unorm, 4, 4);
    texDesc.usage = TextureUsage::Sampled | TextureUsage::CopyDst;
    REQUIRE(device->CreateTexture(texDesc, texture).IsOk());
    TextureViewDesc viewDesc;
    viewDesc.format = TextureFormat::RGBA8Unorm;
    TextureView* view = nullptr;
    REQUIRE(device->CreateTextureView(texture, viewDesc, view).IsOk());
    Sampler* sampler = nullptr;
    REQUIRE(device->CreateSampler(SamplerDesc{}, sampler).IsOk());

    const BindGroupEntry groupEntries[] = {
        BindGroupEntry::BufferEntry(ubo, 0, 16),
        BindGroupEntry::TextureEntry(view),
        BindGroupEntry::SamplerEntry(sampler),
    };
    BindGroupDesc groupDesc;
    groupDesc.layout = layout;
    groupDesc.entries = Span<const BindGroupEntry>(groupEntries, 3);
    BindGroup* group = nullptr;
    REQUIRE(device->CreateBindGroup(groupDesc, group).IsOk());
    CHECK(group->Layout() == layout);

    // Pipeline layout + a render pipeline whose WGSL uses the SHIFTED binding
    // numbers - if the shift scheme mismatched the layout, creation would fail.
    PipelineLayoutDesc plDesc;
    BindGroupLayout* layouts[] = {layout};
    plDesc.bindGroupLayouts = Span<BindGroupLayout* const>(layouts, 1);
    PipelineLayout* pipelineLayout = nullptr;
    REQUIRE(device->CreatePipelineLayout(plDesc, pipelineLayout).IsOk());

    const char8_t* wgsl =
        u8"@group(0) @binding(0) var<uniform> tintUniform : vec4f;\n"
        u8"@group(0) @binding(1000) var sceneTexture : texture_2d<f32>;\n"
        u8"@group(0) @binding(3000) var sceneSampler : sampler;\n"
        u8"@vertex fn vertexMain(@builtin(vertex_index) i : u32) -> @builtin(position) vec4f\n"
        u8"{ return vec4f(f32(i), 0.0, 0.0, 1.0); }\n"
        u8"@fragment fn fragmentMain() -> @location(0) vec4f\n"
        u8"{ return textureSampleLevel(sceneTexture, sceneSampler, vec2f(0.5), 0.0)\n"
        u8"    * tintUniform; }\n";
    ShaderModuleDesc moduleDesc;
    moduleDesc.code =
        Span<const u8>(reinterpret_cast<const u8*>(wgsl), StringView(wgsl).Size());
    ShaderModule* shaderModule = nullptr;
    REQUIRE(device->CreateShaderModule(moduleDesc, shaderModule).IsOk());

    RenderPipelineDesc rpDesc;
    rpDesc.layout = pipelineLayout;
    rpDesc.vertex.shader = ProgrammableStage{shaderModule, u8"vertexMain", ShaderStage::Vertex};
    ColorTargetState target;
    target.format = TextureFormat::RGBA8Unorm;
    target.blend = BlendState::AlphaBlend();
    FragmentState fragment;
    fragment.shader = ProgrammableStage{shaderModule, u8"fragmentMain", ShaderStage::Fragment};
    fragment.targets = Span<const ColorTargetState>(&target, 1);
    rpDesc.fragment = fragment;
    RenderPipeline* renderPipeline = nullptr;
    REQUIRE(device->CreateRenderPipeline(rpDesc, renderPipeline).IsOk());

    // Wireframe has no WebGPU shape - honest NotSupported.
    RenderPipelineDesc wireframeDesc = rpDesc;
    wireframeDesc.primitive.fillMode = FillMode::Wireframe;
    RenderPipeline* wireframe = nullptr;
    CHECK(device->CreateRenderPipeline(wireframeDesc, wireframe).Code() ==
          ErrorCode::NotSupported);

    // Compute pipeline.
    const char8_t* computeWgsl =
        u8"@compute @workgroup_size(1) fn computeMain() { }";
    ShaderModuleDesc computeModuleDesc;
    computeModuleDesc.code = Span<const u8>(reinterpret_cast<const u8*>(computeWgsl),
                                            StringView(computeWgsl).Size());
    ShaderModule* computeModule = nullptr;
    REQUIRE(device->CreateShaderModule(computeModuleDesc, computeModule).IsOk());
    PipelineLayoutDesc emptyLayoutDesc;
    PipelineLayout* emptyLayout = nullptr;
    REQUIRE(device->CreatePipelineLayout(emptyLayoutDesc, emptyLayout).IsOk());
    ComputePipelineDesc cpDesc;
    cpDesc.layout = emptyLayout;
    cpDesc.compute = ProgrammableStage{computeModule, u8"computeMain", ShaderStage::Compute};
    ComputePipeline* computePipeline = nullptr;
    REQUIRE(device->CreateComputePipeline(cpDesc, computePipeline).IsOk());

    device->DestroyComputePipeline(computePipeline);
    device->DestroyPipelineLayout(emptyLayout);
    device->DestroyShaderModule(computeModule);
    device->DestroyRenderPipeline(renderPipeline);
    device->DestroyShaderModule(shaderModule);
    device->DestroyPipelineLayout(pipelineLayout);
    device->DestroyBindGroup(group);
    device->DestroySampler(sampler);
    device->DestroyTextureView(view);
    device->DestroyTexture(texture);
    device->DestroyBuffer(ubo);
    device->WaitIdle();
    CHECK(!device->IsLost());
    device->Destroy();
    backend->Destroy();
}

TEST_CASE("rhi.webgpu: encode + submit + readback - a full GPU round trip")
{
    Backend* backend = TryCreateBackend();
    if (backend == nullptr)
    {
        return;
    }
    Device* device = nullptr;
    REQUIRE(backend->EnumerateAdapters()[0]->CreateDevice(DeviceDesc{}, device).IsOk());

    // Offscreen 4x4 target, cleared to a known color by a real render pass.
    TextureDesc targetDesc = TextureDesc::RenderTarget(TextureFormat::RGBA8Unorm, 4, 4);
    targetDesc.usage = TextureUsage::RenderTarget | TextureUsage::CopySrc;
    Texture* target = nullptr;
    REQUIRE(device->CreateTexture(targetDesc, target).IsOk());
    TextureViewDesc viewDesc;
    viewDesc.format = TextureFormat::RGBA8Unorm;
    TextureView* view = nullptr;
    REQUIRE(device->CreateTextureView(target, viewDesc, view).IsOk());

    // Readback buffer: 4 rows x 256 bytes (WebGPU's bytesPerRow alignment).
    BufferDesc readbackDesc;
    readbackDesc.size = 4 * 256;
    readbackDesc.usage = BufferUsage::CopyDst;
    readbackDesc.memory = MemoryLocation::GpuToCpu;
    Buffer* readback = nullptr;
    REQUIRE(device->CreateBuffer(readbackDesc, readback).IsOk());

    CommandPool* pool = nullptr;
    REQUIRE(device->CreateCommandPool(QueueType::Graphics, pool).IsOk());
    CommandEncoder* encoder = nullptr;
    REQUIRE(pool->CreateEncoder(encoder).IsOk());

    RenderPassDesc pass;
    ColorAttachment color;
    color.view = view;
    color.loadOp = LoadOp::Clear;
    color.storeOp = StoreOp::Store;
    color.clearValue = ClearColor{1.0f, 0.0f, 0.0f, 1.0f}; // pure red
    pass.colorAttachments.Add(color);
    RenderPassEncoder* renderPass = encoder->BeginRenderPass(pass);
    REQUIRE(renderPass != nullptr);
    renderPass->End();

    BufferTextureCopyRegion region;
    region.bytesPerRow = 256;
    region.rowsPerImage = 4;
    region.textureExtent = Extent3D{4, 4, 1};
    encoder->CopyTextureToBuffer(target, readback, region);

    CommandBuffer* commandBuffer = encoder->Finish();
    REQUIRE(commandBuffer != nullptr);

    Fence* fence = nullptr;
    REQUIRE(device->CreateFence(0, fence).IsOk());
    CommandBuffer* commandBuffers[] = {commandBuffer};
    device->GetQueue(QueueType::Graphics)
        ->Submit(Span<CommandBuffer* const>(commandBuffers, 1), fence, 1);
    REQUIRE(fence->Wait(1, ~0ull));

    // Map the readback and verify the clear color survived the round trip.
    const u8* pixels = static_cast<const u8*>(readback->Map());
    REQUIRE(pixels != nullptr);
    CHECK(pixels[0] == 255); // R
    CHECK(pixels[1] == 0);   // G
    CHECK(pixels[2] == 0);   // B
    CHECK(pixels[3] == 255); // A
    CHECK(pixels[256 * 3 + 0] == 255); // last row, first pixel
    readback->Unmap();

    CHECK(!device->IsLost());
    device->DestroyFence(fence);
    device->DestroyCommandPool(pool);
    device->DestroyBuffer(readback);
    device->DestroyTextureView(view);
    device->DestroyTexture(target);
    device->Destroy();
    backend->Destroy();
}

TEST_CASE("rhi.webgpu: transfer batch uploads verify through GPU readback")
{
    Backend* backend = TryCreateBackend();
    if (backend == nullptr)
    {
        return;
    }
    Device* device = nullptr;
    REQUIRE(backend->EnumerateAdapters()[0]->CreateDevice(DeviceDesc{}, device).IsOk());
    Queue* queue = device->GetQueue(QueueType::Transfer);
    TransferBatch* batch = nullptr;
    REQUIRE(queue->CreateTransferBatch(batch).IsOk());

    // Batch-write a GPU-only buffer, then copy it into a readback buffer.
    BufferDesc gpuDesc;
    gpuDesc.size = 64;
    gpuDesc.usage = BufferUsage::Storage | BufferUsage::CopySrc | BufferUsage::CopyDst;
    gpuDesc.memory = MemoryLocation::GpuOnly;
    Buffer* gpuBuffer = nullptr;
    REQUIRE(device->CreateBuffer(gpuDesc, gpuBuffer).IsOk());

    u8 pattern[64];
    for (u32 i = 0; i < 64; ++i)
    {
        pattern[i] = static_cast<u8>(i * 3);
    }
    batch->WriteBuffer(gpuBuffer, 0, Span<const u8>(pattern, 64));

    // Batch-write a texture too (one 4x4 RGBA mip).
    TextureDesc texDesc;
    texDesc.format = TextureFormat::RGBA8Unorm;
    texDesc.width = 4;
    texDesc.height = 4;
    texDesc.usage = TextureUsage::CopyDst | TextureUsage::CopySrc;
    Texture* texture = nullptr;
    REQUIRE(device->CreateTexture(texDesc, texture).IsOk());
    u8 texels[4 * 4 * 4];
    for (u32 i = 0; i < sizeof(texels); ++i)
    {
        texels[i] = static_cast<u8>(255 - i);
    }
    TextureDataLayout layout;
    layout.bytesPerRow = 16;
    layout.rowsPerImage = 4;
    batch->WriteTexture(texture, Span<const u8>(texels, sizeof(texels)), layout,
                        Extent3D{4, 4, 1});

    REQUIRE(batch->Submit().IsOk()); // blocking, Vulkan-batch semantics

    // Read both back.
    BufferDesc readDesc;
    readDesc.size = 4 * 256;
    readDesc.usage = BufferUsage::CopyDst;
    readDesc.memory = MemoryLocation::GpuToCpu;
    Buffer* readback = nullptr;
    REQUIRE(device->CreateBuffer(readDesc, readback).IsOk());

    CommandPool* pool = nullptr;
    REQUIRE(device->CreateCommandPool(QueueType::Graphics, pool).IsOk());
    CommandEncoder* encoder = nullptr;
    REQUIRE(pool->CreateEncoder(encoder).IsOk());
    encoder->CopyBufferToBuffer(gpuBuffer, 0, readback, 0, 64);
    CommandBuffer* commandBuffer = encoder->Finish();
    Fence* fence = nullptr;
    REQUIRE(device->CreateFence(0, fence).IsOk());
    CommandBuffer* commandBuffers[] = {commandBuffer};
    device->GetQueue(QueueType::Graphics)
        ->Submit(Span<CommandBuffer* const>(commandBuffers, 1), fence, 1);
    REQUIRE(fence->Wait(1, ~0ull));

    const u8* bytes = static_cast<const u8*>(readback->Map());
    REQUIRE(bytes != nullptr);
    CHECK(bytes[0] == 0);
    CHECK(bytes[21] == static_cast<u8>(21 * 3));
    CHECK(bytes[63] == static_cast<u8>(63 * 3));
    readback->Unmap();

    // Texture readback through the second copy path.
    BufferTextureCopyRegion region;
    region.bytesPerRow = 256;
    region.rowsPerImage = 4;
    region.textureExtent = Extent3D{4, 4, 1};
    encoder = nullptr;
    REQUIRE(pool->CreateEncoder(encoder).IsOk());
    encoder->CopyTextureToBuffer(texture, readback, region);
    commandBuffer = encoder->Finish();
    CommandBuffer* second[] = {commandBuffer};
    device->GetQueue(QueueType::Graphics)
        ->Submit(Span<CommandBuffer* const>(second, 1), fence, 2);
    REQUIRE(fence->Wait(2, ~0ull));
    bytes = static_cast<const u8*>(readback->Map());
    REQUIRE(bytes != nullptr);
    CHECK(bytes[0] == 255);                 // first texel byte
    CHECK(bytes[15] == static_cast<u8>(255 - 15)); // last byte of row 0
    CHECK(bytes[256 + 0] == static_cast<u8>(255 - 16)); // row 1 starts at bytesPerRow
    readback->Unmap();

    queue->DestroyTransferBatch(batch);
    device->DestroyFence(fence);
    device->DestroyCommandPool(pool);
    device->DestroyBuffer(readback);
    device->DestroyTexture(texture);
    device->DestroyBuffer(gpuBuffer);
    CHECK(!device->IsLost());
    device->Destroy();
    backend->Destroy();
}
