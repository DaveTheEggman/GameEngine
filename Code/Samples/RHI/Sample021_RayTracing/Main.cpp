#include <new>
/// Sample021 -- Ray Tracing (TraceRays). Ported from Sedulous Sample021_RayTracing.
/// Demonstrates hardware ray tracing: builds a BLAS/TLAS for a single triangle,
/// dispatches rays via TraceRays, and copies the RT output to the swap chain.

#include <cstdio>
#include <cstring>

import draconic.core;
import draconic.rhi;
import draconic.shaders;
import draconic.samples.framework;
import draconic.rhi.vk;

namespace sf = draconic::samples::framework;
namespace dr = draconic::rhi;
namespace ds = draconic::shaders;

class RayTracingSample : public sf::SampleApp {
public:
    using sf::SampleApp::SampleApp;
    draconic::core::StringView Title() const override { return u8"Sample021 - Ray Tracing (TraceRays)"; }
protected:
    dr::DeviceFeatures RequiredFeatures() const override {
        dr::DeviceFeatures f{};
        f.rayTracing = true;
        return f;
    }
    draconic::core::Status OnInit() override;
    void OnRender() override;
    void OnResize(draconic::core::u32 w, draconic::core::u32 h) override;
    void OnShutdown() override;
private:
    // Ray tracing shader library (compiled as lib_6_3).
    // All RT entry points are in a single source compiled once as a library.
    static constexpr const char8_t kRtShaderSource[] = u8R"(
        [[vk::image_format("rgba8")]] RWTexture2D<float4> gOutput : register(u0, space0);
        RaytracingAccelerationStructure gScene : register(t0, space0);

        struct RayPayload
        {
            float3 Color;
        };

        [shader("raygeneration")]
        void RayGen()
        {
            uint2 launchIndex = DispatchRaysIndex().xy;
            uint2 launchDim = DispatchRaysDimensions().xy;

            float2 uv = (float2(launchIndex) + 0.5) / float2(launchDim);
            float2 ndc = uv * 2.0 - 1.0;
            ndc.y = -ndc.y;

            RayDesc ray;
            ray.Origin = float3(ndc.x, ndc.y, -1.0);
            ray.Direction = float3(0.0, 0.0, 1.0);
            ray.TMin = 0.001;
            ray.TMax = 100.0;

            RayPayload payload;
            payload.Color = float3(0.0, 0.0, 0.0);

            TraceRay(gScene, RAY_FLAG_FORCE_OPAQUE, 0xFF, 0, 0, 0, ray, payload);

            gOutput[launchIndex] = float4(payload.Color, 1.0);
        }

        [shader("closesthit")]
        void ClosestHit(inout RayPayload payload, BuiltInTriangleIntersectionAttributes attribs)
        {
            float3 bary = float3(1.0 - attribs.barycentrics.x - attribs.barycentrics.y,
                                 attribs.barycentrics.x,
                                 attribs.barycentrics.y);

            payload.Color = float3(bary.x, bary.y, bary.z);
        }

        [shader("miss")]
        void Miss(inout RayPayload payload)
        {
            float2 uv = (float2(DispatchRaysIndex().xy) + 0.5) / float2(DispatchRaysDimensions().xy);
            payload.Color = float3(0.1, 0.1, 0.2) + float3(0.0, 0.0, 0.3) * uv.y;
        }
    )";

    // BLAS triangle positions only (float3 per vertex, no color).
    static constexpr float kBlasVertexData[9] = {
         0.0f,  0.5f, 0.0f,
        -0.5f, -0.5f, 0.0f,
         0.5f, -0.5f, 0.0f
    };

    ds::Compiler* m_compiler = nullptr;

    // RT resources.
    dr::ShaderModule*       m_rtShaderModule    = nullptr;
    dr::RayTracingPipeline* m_rtPipeline        = nullptr;
    dr::AccelStruct*        m_blas              = nullptr;
    dr::AccelStruct*        m_tlas              = nullptr;
    dr::Buffer*             m_scratchBuffer     = nullptr;
    dr::Buffer*             m_rtVertexBuffer    = nullptr;   // Triangle for BLAS.
    dr::Buffer*             m_instanceBuffer    = nullptr;   // TLAS instance data.
    dr::Buffer*             m_sbtBuffer         = nullptr;   // Shader binding table.
    dr::PipelineLayout*     m_rtPipelineLayout  = nullptr;
    dr::BindGroupLayout*    m_rtBindGroupLayout = nullptr;
    dr::BindGroup*          m_rtBindGroup       = nullptr;

    // RT output texture.
    dr::Texture*     m_outputTexture      = nullptr;
    dr::TextureView* m_outputTextureView  = nullptr;
    dr::ResourceState m_outputTextureState = dr::ResourceState::Undefined;

    // SBT layout info (cached for traceRays).
    draconic::core::u32 m_sbtAlignedStride = 0;

    dr::CommandPool* m_pool     = nullptr;
    dr::Fence*       m_fence    = nullptr;
    draconic::core::u64       m_fenceVal = 0;
};

draconic::core::Status RayTracingSample::OnInit() {
    using draconic::core::Status, draconic::core::Span;

    // ---- Check ray tracing support ----
    if (!m_device->features.rayTracing) {
        std::fprintf(stderr, "ERROR: Ray tracing is not supported by this device/backend\n");
        return draconic::core::ErrorCode::Unknown;
    }

    std::printf("Ray tracing extension available:\n");
    std::printf("  shaderGroupHandleSize:      %u\n", m_device->shaderGroupHandleSize);
    std::printf("  shaderGroupHandleAlignment: %u\n", m_device->shaderGroupHandleAlignment);
    std::printf("  shaderGroupBaseAlignment:   %u\n", m_device->shaderGroupBaseAlignment);

    // ---- Shader compiler ----
    if (ds::createCompiler(ds::CompilerDesc{}, m_compiler) != draconic::core::ErrorCode::Ok) return draconic::core::ErrorCode::Unknown;

    // ---- Compile RT shader library (lib_6_3) ----
    // Use ShaderStage::RayGen so stagePrefix yields "lib"; SM 6_3 for RT.
    if (sf::CompileToModule(m_compiler, m_device, kRtShaderSource, ds::ShaderStage::RayGen,
                            u8"", u8"RTShaderLib", u8"6_3", m_rtShaderModule) != draconic::core::ErrorCode::Ok) {
        std::fprintf(stderr, "ERROR: RT shader library compilation failed\n");
        return draconic::core::ErrorCode::Unknown;
    }

    // ---- Command pool and fence ----
    if (m_device->CreateCommandPool(dr::QueueType::Graphics, m_pool) != draconic::core::ErrorCode::Ok) return draconic::core::ErrorCode::Unknown;
    if (m_device->CreateFence(0, m_fence) != draconic::core::ErrorCode::Ok) return draconic::core::ErrorCode::Unknown;

    // ---- Create RT output texture (storage + copy source) ----
    {
        dr::TextureDesc td{};
        td.dimension      = dr::TextureDimension::Texture2D;
        td.format         = dr::TextureFormat::RGBA8Unorm;
        td.width          = m_width;
        td.height         = m_height;
        td.arrayLayerCount = 1;
        td.mipLevelCount  = 1;
        td.sampleCount    = 1;
        td.usage          = dr::TextureUsage::Storage | dr::TextureUsage::CopySrc;
        td.label          = u8"RTOutputTex";
        if (m_device->CreateTexture(td, m_outputTexture) != draconic::core::ErrorCode::Ok) return draconic::core::ErrorCode::Unknown;

        dr::TextureViewDesc tvd{};
        tvd.label = u8"RTOutputView";
        if (m_device->CreateTextureView(m_outputTexture, tvd, m_outputTextureView) != draconic::core::ErrorCode::Ok) return draconic::core::ErrorCode::Unknown;
    }

    // ---- Create BLAS vertex buffer (3 vertices * 12 bytes = 36 bytes) ----
    {
        dr::BufferDesc bd{};
        bd.size   = 36;
        bd.usage  = dr::BufferUsage::AccelStructInput | dr::BufferUsage::CopyDst;
        bd.memory = dr::MemoryLocation::GpuOnly;
        bd.label  = u8"BLAS_VB";
        if (m_device->CreateBuffer(bd, m_rtVertexBuffer) != draconic::core::ErrorCode::Ok) return draconic::core::ErrorCode::Unknown;
    }

    // Upload BLAS vertex data.
    {
        dr::TransferBatch* transfer = nullptr;
        if (m_graphicsQueue->CreateTransferBatch(transfer) != draconic::core::ErrorCode::Ok) return draconic::core::ErrorCode::Unknown;
        transfer->WriteBuffer(m_rtVertexBuffer, 0,
            Span<const draconic::core::u8>(reinterpret_cast<const draconic::core::u8*>(kBlasVertexData), 36));
        transfer->Submit();
        m_graphicsQueue->DestroyTransferBatch(transfer);
    }

    // ---- Create acceleration structures ----
    {
        dr::AccelStructDesc asd{};
        asd.type  = dr::AccelStructType::BottomLevel;
        asd.label = u8"BLAS";
        if (m_device->CreateAccelStruct(asd, m_blas) != draconic::core::ErrorCode::Ok) return draconic::core::ErrorCode::Unknown;

        asd.type  = dr::AccelStructType::TopLevel;
        asd.label = u8"TLAS";
        if (m_device->CreateAccelStruct(asd, m_tlas) != draconic::core::ErrorCode::Ok) return draconic::core::ErrorCode::Unknown;
    }

    // ---- Create scratch buffer (256 KB) ----
    {
        dr::BufferDesc bd{};
        bd.size   = 256 * 1024;
        bd.usage  = dr::BufferUsage::AccelStructScratch;
        bd.memory = dr::MemoryLocation::GpuOnly;
        bd.label  = u8"ScratchBuffer";
        if (m_device->CreateBuffer(bd, m_scratchBuffer) != draconic::core::ErrorCode::Ok) return draconic::core::ErrorCode::Unknown;
    }

    // ---- Create instance buffer (64 bytes = sizeof(VkAccelerationStructureInstanceKHR)) ----
    {
        dr::BufferDesc bd{};
        bd.size   = 64;
        bd.usage  = dr::BufferUsage::AccelStructInput;
        bd.memory = dr::MemoryLocation::CpuToGpu;
        bd.label  = u8"InstanceBuffer";
        if (m_device->CreateBuffer(bd, m_instanceBuffer) != draconic::core::ErrorCode::Ok) return draconic::core::ErrorCode::Unknown;
    }

    // Fill instance data.
    {
        auto* ptr = static_cast<draconic::core::u8*>(m_instanceBuffer->Map());
        if (!ptr) { std::fprintf(stderr, "ERROR: Failed to map instance buffer\n"); return draconic::core::ErrorCode::Unknown; }
        std::memset(ptr, 0, 64);

        // Identity transform (3x4 row-major float matrix).
        auto* transform = reinterpret_cast<float*>(ptr);
        transform[0]  = 1.0f;  // row 0, col 0
        transform[5]  = 1.0f;  // row 1, col 1
        transform[10] = 1.0f;  // row 2, col 2

        // instanceCustomIndex (24 bit) + mask (8 bit) at offset 48.
        ptr[48] = 0; ptr[49] = 0; ptr[50] = 0; // customIndex = 0
        ptr[51] = 0xFF; // mask

        // SBT offset (24 bit) + flags (8 bit) at offset 52.
        ptr[52] = 0; ptr[53] = 0; ptr[54] = 0; // sbtOffset = 0
        ptr[55] = 0x04; // VK_GEOMETRY_INSTANCE_FORCE_OPAQUE_BIT_KHR

        // accelerationStructureReference at offset 56.
        *reinterpret_cast<draconic::core::u64*>(ptr + 56) = m_blas->DeviceAddress();

        m_instanceBuffer->Unmap();
    }

    // ---- Build BLAS and TLAS ----
    {
        dr::CommandEncoder* encoder = nullptr;
        if (m_pool->CreateEncoder(encoder) != draconic::core::ErrorCode::Ok) return draconic::core::ErrorCode::Unknown;

        if (auto* rtEnc = encoder->AsRayTracingExt()) {
            // Build BLAS from triangle geometry.
            dr::AccelStructGeometryTriangles triGeom{};
            triGeom.vertexBuffer = m_rtVertexBuffer;
            triGeom.vertexOffset = 0;
            triGeom.vertexCount  = 3;
            triGeom.vertexStride = 12;
            triGeom.vertexFormat = dr::VertexFormat::Float32x3;
            triGeom.flags        = dr::GeometryFlags::Opaque;

            rtEnc->BuildBottomLevelAccelStruct(m_blas, m_scratchBuffer, 0,
                Span<const dr::AccelStructGeometryTriangles>(&triGeom, 1),
                Span<const dr::AccelStructGeometryAABBs>{});

            // Barrier between BLAS and TLAS build.
            dr::MemoryBarrier mb{};
            mb.oldState = dr::ResourceState::AccelStructWrite;
            mb.newState = dr::ResourceState::AccelStructRead;
            dr::BarrierGroup bg{};
            bg.memoryBarriers = Span<const dr::MemoryBarrier>(&mb, 1);
            encoder->Barrier(bg);

            // Build TLAS from instances.
            rtEnc->BuildTopLevelAccelStruct(m_tlas, m_scratchBuffer, 0,
                m_instanceBuffer, 0, 1);
        } else {
            std::fprintf(stderr, "ERROR: Command encoder does not support ray tracing\n");
            m_pool->DestroyEncoder(encoder);
            return draconic::core::ErrorCode::Unknown;
        }

        dr::CommandBuffer* cb = encoder->Finish();
        m_fenceVal++;
        dr::CommandBuffer* cbs[1] = { cb };
        m_graphicsQueue->Submit(Span<dr::CommandBuffer* const>(cbs, 1), m_fence, m_fenceVal);

        // Wait for build to complete.
        m_fence->Wait(m_fenceVal);
        m_pool->Reset();
        m_pool->DestroyEncoder(encoder);
    }

    std::printf("BLAS and TLAS built successfully.\n");
    std::printf("  BLAS DeviceAddress: 0x%llx\n", static_cast<unsigned long long>(m_blas->DeviceAddress()));
    std::printf("  TLAS DeviceAddress: 0x%llx\n", static_cast<unsigned long long>(m_tlas->DeviceAddress()));

    // ---- Create RT bind group layout and bind group ----
    {
        // binding 0 (u0): RWTexture2D - storage texture, read-write
        // binding 0 (t0): RaytracingAccelerationStructure - TLAS
        // Both use register 0 in different HLSL spaces (u vs t),
        // mapped to different Vulkan bindings via shifts (UAV=2000, SRV=1000).
        dr::BindGroupLayoutEntry layoutEntries[2]{};

        // Storage texture (read-write).
        layoutEntries[0].binding     = 0;
        layoutEntries[0].visibility  = dr::ShaderStage::RayGen;
        layoutEntries[0].type        = dr::BindingType::StorageTextureReadWrite;
        layoutEntries[0].storageTextureFormat = dr::TextureFormat::RGBA8Unorm;
        layoutEntries[0].count       = 1;

        // Acceleration structure.
        layoutEntries[1].binding     = 0;
        layoutEntries[1].visibility  = dr::ShaderStage::RayGen;
        layoutEntries[1].type        = dr::BindingType::AccelerationStructure;
        layoutEntries[1].count       = 1;

        dr::BindGroupLayoutDesc bgld{};
        bgld.entries = Span<const dr::BindGroupLayoutEntry>(layoutEntries, 2);
        bgld.label   = u8"RTBindGroupLayout";
        if (m_device->CreateBindGroupLayout(bgld, m_rtBindGroupLayout) != draconic::core::ErrorCode::Ok) return draconic::core::ErrorCode::Unknown;

        // Create bind group with output texture + TLAS.
        dr::BindGroupEntry bgEntries[2]{};
        bgEntries[0] = dr::BindGroupEntry::TextureEntry(m_outputTextureView);
        bgEntries[1] = dr::BindGroupEntry::AccelStructEntry(m_tlas);

        dr::BindGroupDesc bgd{};
        bgd.layout  = m_rtBindGroupLayout;
        bgd.entries = Span<const dr::BindGroupEntry>(bgEntries, 2);
        bgd.label   = u8"RTBindGroup";
        if (m_device->CreateBindGroup(bgd, m_rtBindGroup) != draconic::core::ErrorCode::Ok) return draconic::core::ErrorCode::Unknown;
    }

    // ---- Create RT pipeline layout with bind group ----
    {
        dr::BindGroupLayout* bglArr[1] = { m_rtBindGroupLayout };

        dr::PipelineLayoutDesc pld{};
        pld.bindGroupLayouts = Span<dr::BindGroupLayout* const>(bglArr, 1);
        pld.label = u8"RTPipelineLayout";
        if (m_device->CreatePipelineLayout(pld, m_rtPipelineLayout) != draconic::core::ErrorCode::Ok) return draconic::core::ErrorCode::Unknown;

        // 3 stages: RayGen, ClosestHit, Miss - all from the same shader module.
        dr::ProgrammableStage stages[3]{};
        stages[0] = { m_rtShaderModule, u8"RayGen",     dr::ShaderStage::RayGen };
        stages[1] = { m_rtShaderModule, u8"ClosestHit", dr::ShaderStage::ClosestHit };
        stages[2] = { m_rtShaderModule, u8"Miss",       dr::ShaderStage::Miss };

        // 3 groups: raygen (general), hit group (triangles), miss (general).
        dr::RayTracingShaderGroup groups[3]{};
        groups[0].type               = dr::RayTracingShaderGroup::Type::General;
        groups[0].generalShaderIndex = 0;

        groups[1].type                   = dr::RayTracingShaderGroup::Type::TrianglesHitGroup;
        groups[1].closestHitShaderIndex  = 1;

        groups[2].type               = dr::RayTracingShaderGroup::Type::General;
        groups[2].generalShaderIndex = 2;

        dr::RayTracingPipelineDesc rtpd{};
        rtpd.layout           = m_rtPipelineLayout;
        rtpd.stages           = Span<const dr::ProgrammableStage>(stages, 3);
        rtpd.groups           = Span<const dr::RayTracingShaderGroup>(groups, 3);
        rtpd.maxRecursionDepth = 1;
        rtpd.label            = u8"RTPipeline";
        if (m_device->CreateRayTracingPipeline(rtpd, m_rtPipeline) != draconic::core::ErrorCode::Ok) return draconic::core::ErrorCode::Unknown;
    }

    std::printf("Ray tracing pipeline created successfully.\n");

    // ---- Build Shader Binding Table ----
    {
        draconic::core::u32 handleSize     = m_device->shaderGroupHandleSize;
        draconic::core::u32 baseAlignment  = m_device->shaderGroupBaseAlignment;
        draconic::core::u32 groupCount     = 3;

        // Aligned handle stride (round up to base alignment).
        m_sbtAlignedStride = (handleSize + baseAlignment - 1) & ~(baseAlignment - 1);

        // Get shader group handles.
        draconic::core::u8 handleData[128]; // Enough for 3 handles (max ~32 bytes each).
        if (m_device->GetShaderGroupHandles(m_rtPipeline, 0, groupCount,
                Span<draconic::core::u8>(handleData, handleSize * groupCount)) != draconic::core::ErrorCode::Ok) {
            std::fprintf(stderr, "ERROR: getShaderGroupHandles failed\n");
            return draconic::core::ErrorCode::Unknown;
        }

        // Create SBT buffer: 3 entries, each aligned to baseAlignment.
        draconic::core::u64 sbtSize = static_cast<draconic::core::u64>(m_sbtAlignedStride) * groupCount;
        dr::BufferDesc sbd{};
        sbd.size   = sbtSize;
        sbd.usage  = dr::BufferUsage::ShaderBindingTable;
        sbd.memory = dr::MemoryLocation::CpuToGpu;
        sbd.label  = u8"SBTBuffer";
        if (m_device->CreateBuffer(sbd, m_sbtBuffer) != draconic::core::ErrorCode::Ok) return draconic::core::ErrorCode::Unknown;

        // Copy handles into SBT with proper alignment.
        auto* sbtPtr = static_cast<draconic::core::u8*>(m_sbtBuffer->Map());
        if (!sbtPtr) { std::fprintf(stderr, "ERROR: Failed to map SBT buffer\n"); return draconic::core::ErrorCode::Unknown; }
        std::memset(sbtPtr, 0, static_cast<size_t>(sbtSize));

        for (draconic::core::u32 i = 0; i < groupCount; i++) {
            std::memcpy(sbtPtr + (i * m_sbtAlignedStride),
                        handleData + (i * handleSize),
                        handleSize);
        }
        m_sbtBuffer->Unmap();

        std::printf("SBT built: handleSize=%u, baseAlignment=%u, alignedStride=%u, totalSize=%llu\n",
            handleSize, baseAlignment, m_sbtAlignedStride, static_cast<unsigned long long>(sbtSize));
    }

    std::printf("RT sample ready - TraceRays rendering active.\n");

    return draconic::core::ErrorCode::Ok;
}

void RayTracingSample::OnRender() {
    using draconic::core::Span;

    // Wait for previous frame.
    if (m_fenceVal > 0) m_fence->Wait(m_fenceVal, ~0ull);

    // Acquire next swap chain image.
    if (m_swapChain->AcquireNextImage() != draconic::core::ErrorCode::Ok) return;

    // Reset and create encoder.
    m_pool->Reset();
    dr::CommandEncoder* enc = nullptr;
    if (m_pool->CreateEncoder(enc) != draconic::core::ErrorCode::Ok || !enc) return;

    // ---- Transition output texture to ShaderWrite for TraceRays ----
    enc->TransitionTexture(m_outputTexture, m_outputTextureState, dr::ResourceState::ShaderWrite);

    // ---- Dispatch TraceRays ----
    if (auto* rtEnc = enc->AsRayTracingExt()) {
        rtEnc->SetRayTracingPipeline(m_rtPipeline);
        rtEnc->SetBindGroup(0, m_rtBindGroup);

        // SBT layout: [0] = raygen, [1] = hit, [2] = miss.
        draconic::core::u64 raygenOffset = 0;
        draconic::core::u64 hitOffset    = static_cast<draconic::core::u64>(1) * m_sbtAlignedStride;
        draconic::core::u64 missOffset   = static_cast<draconic::core::u64>(2) * m_sbtAlignedStride;
        draconic::core::u64 stride       = static_cast<draconic::core::u64>(m_sbtAlignedStride);

        rtEnc->TraceRays(
            m_sbtBuffer, raygenOffset, stride,
            m_sbtBuffer, missOffset, stride,
            m_sbtBuffer, hitOffset, stride,
            m_width, m_height);
    }

    // ---- Transition: output texture ShaderWrite -> CopySrc, swapchain Present -> CopyDst ----
    {
        dr::TextureBarrier texBarriers[2]{};
        texBarriers[0].texture  = m_outputTexture;
        texBarriers[0].oldState = dr::ResourceState::ShaderWrite;
        texBarriers[0].newState = dr::ResourceState::CopySrc;

        texBarriers[1].texture  = m_swapChain->CurrentTexture();
        texBarriers[1].oldState = dr::ResourceState::Present;
        texBarriers[1].newState = dr::ResourceState::CopyDst;

        dr::BarrierGroup bg{};
        bg.textureBarriers = Span<const dr::TextureBarrier>(texBarriers, 2);
        enc->Barrier(bg);
    }

    // ---- Copy RT output to swapchain ----
    m_outputTextureState = dr::ResourceState::CopySrc;
    {
        dr::TextureCopyRegion region{};
        region.extent = dr::Extent3D{ m_width, m_height, 1 };
        enc->CopyTextureToTexture(m_outputTexture, m_swapChain->CurrentTexture(), region);
    }

    // ---- Transition swapchain CopyDst -> Present ----
    enc->TransitionTexture(m_swapChain->CurrentTexture(),
                           dr::ResourceState::CopyDst, dr::ResourceState::Present);

    // Finish and submit.
    dr::CommandBuffer* cb = enc->Finish();
    m_fenceVal++;
    dr::CommandBuffer* cbs[1] = { cb };
    m_graphicsQueue->Submit(Span<dr::CommandBuffer* const>(cbs, 1), m_fence, m_fenceVal);

    // Present.
    m_swapChain->Present(m_graphicsQueue);
    m_pool->DestroyEncoder(enc);
}

void RayTracingSample::OnResize(draconic::core::u32 w, draconic::core::u32 h) {
    using draconic::core::Status, draconic::core::Span;
    // Wait for GPU idle before destroying resources.
    if (m_fence) m_fence->Wait(m_fenceVal, ~0ull);

    // Destroy old bind group, view, and texture.
    if (m_rtBindGroup)      m_device->DestroyBindGroup(m_rtBindGroup);
    if (m_outputTextureView) m_device->DestroyTextureView(m_outputTextureView);
    if (m_outputTexture)     m_device->DestroyTexture(m_outputTexture);
    m_rtBindGroup = nullptr; m_outputTextureView = nullptr; m_outputTexture = nullptr;

    // Recreate output texture at new size.
    dr::TextureDesc td{};
    td.dimension       = dr::TextureDimension::Texture2D;
    td.format          = dr::TextureFormat::RGBA8Unorm;
    td.width           = w;
    td.height          = h;
    td.arrayLayerCount = 1;
    td.mipLevelCount   = 1;
    td.sampleCount     = 1;
    td.usage           = dr::TextureUsage::Storage | dr::TextureUsage::CopySrc;
    td.label           = u8"RTOutputTex";
    m_device->CreateTexture(td, m_outputTexture);

    dr::TextureViewDesc tvd{}; tvd.label = u8"RTOutputView";
    m_device->CreateTextureView(m_outputTexture, tvd, m_outputTextureView);

    // Recreate bind group with new texture view + same TLAS.
    dr::BindGroupEntry bgEntries[2]{};
    bgEntries[0] = dr::BindGroupEntry::TextureEntry(m_outputTextureView);
    bgEntries[1] = dr::BindGroupEntry::AccelStructEntry(m_tlas);
    dr::BindGroupDesc bgd{};
    bgd.layout  = m_rtBindGroupLayout;
    bgd.entries = Span<const dr::BindGroupEntry>(bgEntries, 2);
    bgd.label   = u8"RTBindGroup";
    m_device->CreateBindGroup(bgd, m_rtBindGroup);

    m_outputTextureState = dr::ResourceState::Undefined;
}

void RayTracingSample::OnShutdown() {
    if (m_fence) m_fence->Wait(m_fenceVal, ~0ull);

    // RT bind group.
    if (m_rtBindGroup)       m_device->DestroyBindGroup(m_rtBindGroup);
    if (m_rtBindGroupLayout) m_device->DestroyBindGroupLayout(m_rtBindGroupLayout);

    // RT output texture.
    if (m_outputTextureView) m_device->DestroyTextureView(m_outputTextureView);
    if (m_outputTexture)     m_device->DestroyTexture(m_outputTexture);

    // RT resources.
    if (m_sbtBuffer)         m_device->DestroyBuffer(m_sbtBuffer);
    if (m_rtPipeline)        m_device->DestroyRayTracingPipeline(m_rtPipeline);
    if (m_rtPipelineLayout)  m_device->DestroyPipelineLayout(m_rtPipelineLayout);
    if (m_instanceBuffer)    m_device->DestroyBuffer(m_instanceBuffer);
    if (m_scratchBuffer)     m_device->DestroyBuffer(m_scratchBuffer);
    if (m_tlas)              m_device->DestroyAccelStruct(m_tlas);
    if (m_blas)              m_device->DestroyAccelStruct(m_blas);
    if (m_rtVertexBuffer)    m_device->DestroyBuffer(m_rtVertexBuffer);
    if (m_rtShaderModule)    m_device->DestroyShaderModule(m_rtShaderModule);

    if (m_fence) m_device->DestroyFence(m_fence);
    if (m_pool)  m_device->DestroyCommandPool(m_pool);

    if (m_compiler) { m_compiler->Destroy(); delete m_compiler; }
}

int main(int argc, char** argv) { RayTracingSample app; return app.Run(argc, argv); }
