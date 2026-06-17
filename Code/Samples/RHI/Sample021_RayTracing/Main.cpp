#include <new>
/// Sample021 -- Ray Tracing (TraceRays). Ported from Sedulous Sample021_RayTracing.
/// Demonstrates hardware ray tracing: builds a BLAS/TLAS for a single triangle,
/// dispatches rays via TraceRays, and copies the RT output to the swap chain.

#include <cstdio>
#include <cstring>

import raptor.core;
import raptor.rhi;
import raptor.shaders;
import raptor.samples.framework;
import raptor.rhi.vk;

namespace sf = raptor::samples::framework;
namespace dr = raptor::rhi;
namespace ds = raptor::shaders;

class RayTracingSample : public sf::SampleApp {
public:
    using sf::SampleApp::SampleApp;
    raptor::core::StringView title() const override { return u"Sample021 - Ray Tracing (TraceRays)"; }
protected:
    dr::DeviceFeatures requiredFeatures() const override {
        dr::DeviceFeatures f{};
        f.rayTracing = true;
        return f;
    }
    raptor::core::Status onInit() override;
    void onRender() override;
    void onResize(raptor::core::u32 w, raptor::core::u32 h) override;
    void onShutdown() override;
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

    ds::Compiler* compiler_ = nullptr;

    // RT resources.
    dr::ShaderModule*       rtShaderModule_    = nullptr;
    dr::RayTracingPipeline* rtPipeline_        = nullptr;
    dr::AccelStruct*        blas_              = nullptr;
    dr::AccelStruct*        tlas_              = nullptr;
    dr::Buffer*             scratchBuffer_     = nullptr;
    dr::Buffer*             rtVertexBuffer_    = nullptr;   // Triangle for BLAS.
    dr::Buffer*             instanceBuffer_    = nullptr;   // TLAS instance data.
    dr::Buffer*             sbtBuffer_         = nullptr;   // Shader binding table.
    dr::PipelineLayout*     rtPipelineLayout_  = nullptr;
    dr::BindGroupLayout*    rtBindGroupLayout_ = nullptr;
    dr::BindGroup*          rtBindGroup_       = nullptr;

    // RT output texture.
    dr::Texture*     outputTexture_      = nullptr;
    dr::TextureView* outputTextureView_  = nullptr;
    dr::ResourceState outputTextureState_ = dr::ResourceState::Undefined;

    // SBT layout info (cached for traceRays).
    raptor::core::u32 sbtAlignedStride_ = 0;

    dr::CommandPool* pool_     = nullptr;
    dr::Fence*       fence_    = nullptr;
    raptor::core::u64       fenceVal_ = 0;
};

raptor::core::Status RayTracingSample::onInit() {
    using raptor::core::Status, raptor::core::Span;

    // ---- Check ray tracing support ----
    if (!device_->features.rayTracing) {
        std::fprintf(stderr, "ERROR: Ray tracing is not supported by this device/backend\n");
        return raptor::core::ErrorCode::Unknown;
    }

    std::printf("Ray tracing extension available:\n");
    std::printf("  shaderGroupHandleSize:      %u\n", device_->shaderGroupHandleSize);
    std::printf("  shaderGroupHandleAlignment: %u\n", device_->shaderGroupHandleAlignment);
    std::printf("  shaderGroupBaseAlignment:   %u\n", device_->shaderGroupBaseAlignment);

    // ---- Shader compiler ----
    if (ds::createCompiler(ds::CompilerDesc{}, compiler_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;

    // ---- Compile RT shader library (lib_6_3) ----
    // Use ShaderStage::RayGen so stagePrefix yields "lib"; SM 6_3 for RT.
    if (sf::compileToModule(compiler_, device_, kRtShaderSource, ds::ShaderStage::RayGen,
                            u"", u"RTShaderLib", u"6_3", rtShaderModule_) != raptor::core::ErrorCode::Ok) {
        std::fprintf(stderr, "ERROR: RT shader library compilation failed\n");
        return raptor::core::ErrorCode::Unknown;
    }

    // ---- Command pool and fence ----
    if (device_->createCommandPool(dr::QueueType::Graphics, pool_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;
    if (device_->createFence(0, fence_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;

    // ---- Create RT output texture (storage + copy source) ----
    {
        dr::TextureDesc td{};
        td.dimension      = dr::TextureDimension::Texture2D;
        td.format         = dr::TextureFormat::RGBA8Unorm;
        td.width          = width_;
        td.height         = height_;
        td.arrayLayerCount = 1;
        td.mipLevelCount  = 1;
        td.sampleCount    = 1;
        td.usage          = dr::TextureUsage::Storage | dr::TextureUsage::CopySrc;
        td.label          = u"RTOutputTex";
        if (device_->createTexture(td, outputTexture_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;

        dr::TextureViewDesc tvd{};
        tvd.label = u"RTOutputView";
        if (device_->createTextureView(outputTexture_, tvd, outputTextureView_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;
    }

    // ---- Create BLAS vertex buffer (3 vertices * 12 bytes = 36 bytes) ----
    {
        dr::BufferDesc bd{};
        bd.size   = 36;
        bd.usage  = dr::BufferUsage::AccelStructInput | dr::BufferUsage::CopyDst;
        bd.memory = dr::MemoryLocation::GpuOnly;
        bd.label  = u"BLAS_VB";
        if (device_->createBuffer(bd, rtVertexBuffer_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;
    }

    // Upload BLAS vertex data.
    {
        dr::TransferBatch* transfer = nullptr;
        if (graphicsQueue_->createTransferBatch(transfer) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;
        transfer->writeBuffer(rtVertexBuffer_, 0,
            Span<const raptor::core::u8>(reinterpret_cast<const raptor::core::u8*>(kBlasVertexData), 36));
        transfer->submit();
        graphicsQueue_->destroyTransferBatch(transfer);
    }

    // ---- Create acceleration structures ----
    {
        dr::AccelStructDesc asd{};
        asd.type  = dr::AccelStructType::BottomLevel;
        asd.label = u"BLAS";
        if (device_->createAccelStruct(asd, blas_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;

        asd.type  = dr::AccelStructType::TopLevel;
        asd.label = u"TLAS";
        if (device_->createAccelStruct(asd, tlas_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;
    }

    // ---- Create scratch buffer (256 KB) ----
    {
        dr::BufferDesc bd{};
        bd.size   = 256 * 1024;
        bd.usage  = dr::BufferUsage::AccelStructScratch;
        bd.memory = dr::MemoryLocation::GpuOnly;
        bd.label  = u"ScratchBuffer";
        if (device_->createBuffer(bd, scratchBuffer_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;
    }

    // ---- Create instance buffer (64 bytes = sizeof(VkAccelerationStructureInstanceKHR)) ----
    {
        dr::BufferDesc bd{};
        bd.size   = 64;
        bd.usage  = dr::BufferUsage::AccelStructInput;
        bd.memory = dr::MemoryLocation::CpuToGpu;
        bd.label  = u"InstanceBuffer";
        if (device_->createBuffer(bd, instanceBuffer_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;
    }

    // Fill instance data.
    {
        auto* ptr = static_cast<raptor::core::u8*>(instanceBuffer_->map());
        if (!ptr) { std::fprintf(stderr, "ERROR: Failed to map instance buffer\n"); return raptor::core::ErrorCode::Unknown; }
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
        *reinterpret_cast<raptor::core::u64*>(ptr + 56) = blas_->deviceAddress();

        instanceBuffer_->unmap();
    }

    // ---- Build BLAS and TLAS ----
    {
        dr::CommandEncoder* encoder = nullptr;
        if (pool_->createEncoder(encoder) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;

        if (auto* rtEnc = encoder->asRayTracingExt()) {
            // Build BLAS from triangle geometry.
            dr::AccelStructGeometryTriangles triGeom{};
            triGeom.vertexBuffer = rtVertexBuffer_;
            triGeom.vertexOffset = 0;
            triGeom.vertexCount  = 3;
            triGeom.vertexStride = 12;
            triGeom.vertexFormat = dr::VertexFormat::Float32x3;
            triGeom.flags        = dr::GeometryFlags::Opaque;

            rtEnc->buildBottomLevelAccelStruct(blas_, scratchBuffer_, 0,
                Span<const dr::AccelStructGeometryTriangles>(&triGeom, 1),
                Span<const dr::AccelStructGeometryAABBs>{});

            // Barrier between BLAS and TLAS build.
            dr::MemoryBarrier mb{};
            mb.oldState = dr::ResourceState::AccelStructWrite;
            mb.newState = dr::ResourceState::AccelStructRead;
            dr::BarrierGroup bg{};
            bg.memoryBarriers = Span<const dr::MemoryBarrier>(&mb, 1);
            encoder->barrier(bg);

            // Build TLAS from instances.
            rtEnc->buildTopLevelAccelStruct(tlas_, scratchBuffer_, 0,
                instanceBuffer_, 0, 1);
        } else {
            std::fprintf(stderr, "ERROR: Command encoder does not support ray tracing\n");
            pool_->destroyEncoder(encoder);
            return raptor::core::ErrorCode::Unknown;
        }

        dr::CommandBuffer* cb = encoder->finish();
        fenceVal_++;
        dr::CommandBuffer* cbs[1] = { cb };
        graphicsQueue_->submit(Span<dr::CommandBuffer* const>(cbs, 1), fence_, fenceVal_);

        // Wait for build to complete.
        fence_->wait(fenceVal_);
        pool_->reset();
        pool_->destroyEncoder(encoder);
    }

    std::printf("BLAS and TLAS built successfully.\n");
    std::printf("  BLAS DeviceAddress: 0x%llx\n", static_cast<unsigned long long>(blas_->deviceAddress()));
    std::printf("  TLAS DeviceAddress: 0x%llx\n", static_cast<unsigned long long>(tlas_->deviceAddress()));

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
        bgld.label   = u"RTBindGroupLayout";
        if (device_->createBindGroupLayout(bgld, rtBindGroupLayout_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;

        // Create bind group with output texture + TLAS.
        dr::BindGroupEntry bgEntries[2]{};
        bgEntries[0] = dr::BindGroupEntry::textureEntry(outputTextureView_);
        bgEntries[1] = dr::BindGroupEntry::accelStructEntry(tlas_);

        dr::BindGroupDesc bgd{};
        bgd.layout  = rtBindGroupLayout_;
        bgd.entries = Span<const dr::BindGroupEntry>(bgEntries, 2);
        bgd.label   = u"RTBindGroup";
        if (device_->createBindGroup(bgd, rtBindGroup_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;
    }

    // ---- Create RT pipeline layout with bind group ----
    {
        dr::BindGroupLayout* bglArr[1] = { rtBindGroupLayout_ };

        dr::PipelineLayoutDesc pld{};
        pld.bindGroupLayouts = Span<dr::BindGroupLayout* const>(bglArr, 1);
        pld.label = u"RTPipelineLayout";
        if (device_->createPipelineLayout(pld, rtPipelineLayout_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;

        // 3 stages: RayGen, ClosestHit, Miss - all from the same shader module.
        dr::ProgrammableStage stages[3]{};
        stages[0] = { rtShaderModule_, u"RayGen",     dr::ShaderStage::RayGen };
        stages[1] = { rtShaderModule_, u"ClosestHit", dr::ShaderStage::ClosestHit };
        stages[2] = { rtShaderModule_, u"Miss",       dr::ShaderStage::Miss };

        // 3 groups: raygen (general), hit group (triangles), miss (general).
        dr::RayTracingShaderGroup groups[3]{};
        groups[0].type               = dr::RayTracingShaderGroup::Type::General;
        groups[0].generalShaderIndex = 0;

        groups[1].type                   = dr::RayTracingShaderGroup::Type::TrianglesHitGroup;
        groups[1].closestHitShaderIndex  = 1;

        groups[2].type               = dr::RayTracingShaderGroup::Type::General;
        groups[2].generalShaderIndex = 2;

        dr::RayTracingPipelineDesc rtpd{};
        rtpd.layout           = rtPipelineLayout_;
        rtpd.stages           = Span<const dr::ProgrammableStage>(stages, 3);
        rtpd.groups           = Span<const dr::RayTracingShaderGroup>(groups, 3);
        rtpd.maxRecursionDepth = 1;
        rtpd.label            = u"RTPipeline";
        if (device_->createRayTracingPipeline(rtpd, rtPipeline_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;
    }

    std::printf("Ray tracing pipeline created successfully.\n");

    // ---- Build Shader Binding Table ----
    {
        raptor::core::u32 handleSize     = device_->shaderGroupHandleSize;
        raptor::core::u32 baseAlignment  = device_->shaderGroupBaseAlignment;
        raptor::core::u32 groupCount     = 3;

        // Aligned handle stride (round up to base alignment).
        sbtAlignedStride_ = (handleSize + baseAlignment - 1) & ~(baseAlignment - 1);

        // Get shader group handles.
        raptor::core::u8 handleData[128]; // Enough for 3 handles (max ~32 bytes each).
        if (device_->getShaderGroupHandles(rtPipeline_, 0, groupCount,
                Span<raptor::core::u8>(handleData, handleSize * groupCount)) != raptor::core::ErrorCode::Ok) {
            std::fprintf(stderr, "ERROR: getShaderGroupHandles failed\n");
            return raptor::core::ErrorCode::Unknown;
        }

        // Create SBT buffer: 3 entries, each aligned to baseAlignment.
        raptor::core::u64 sbtSize = static_cast<raptor::core::u64>(sbtAlignedStride_) * groupCount;
        dr::BufferDesc sbd{};
        sbd.size   = sbtSize;
        sbd.usage  = dr::BufferUsage::ShaderBindingTable;
        sbd.memory = dr::MemoryLocation::CpuToGpu;
        sbd.label  = u"SBTBuffer";
        if (device_->createBuffer(sbd, sbtBuffer_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;

        // Copy handles into SBT with proper alignment.
        auto* sbtPtr = static_cast<raptor::core::u8*>(sbtBuffer_->map());
        if (!sbtPtr) { std::fprintf(stderr, "ERROR: Failed to map SBT buffer\n"); return raptor::core::ErrorCode::Unknown; }
        std::memset(sbtPtr, 0, static_cast<size_t>(sbtSize));

        for (raptor::core::u32 i = 0; i < groupCount; i++) {
            std::memcpy(sbtPtr + (i * sbtAlignedStride_),
                        handleData + (i * handleSize),
                        handleSize);
        }
        sbtBuffer_->unmap();

        std::printf("SBT built: handleSize=%u, baseAlignment=%u, alignedStride=%u, totalSize=%llu\n",
            handleSize, baseAlignment, sbtAlignedStride_, static_cast<unsigned long long>(sbtSize));
    }

    std::printf("RT sample ready - TraceRays rendering active.\n");

    return raptor::core::ErrorCode::Ok;
}

void RayTracingSample::onRender() {
    using raptor::core::Span;

    // Wait for previous frame.
    if (fenceVal_ > 0) fence_->wait(fenceVal_, ~0ull);

    // Acquire next swap chain image.
    if (swapChain_->acquireNextImage() != raptor::core::ErrorCode::Ok) return;

    // Reset and create encoder.
    pool_->reset();
    dr::CommandEncoder* enc = nullptr;
    if (pool_->createEncoder(enc) != raptor::core::ErrorCode::Ok || !enc) return;

    // ---- Transition output texture to ShaderWrite for TraceRays ----
    enc->transitionTexture(outputTexture_, outputTextureState_, dr::ResourceState::ShaderWrite);

    // ---- Dispatch TraceRays ----
    if (auto* rtEnc = enc->asRayTracingExt()) {
        rtEnc->setRayTracingPipeline(rtPipeline_);
        rtEnc->setBindGroup(0, rtBindGroup_);

        // SBT layout: [0] = raygen, [1] = hit, [2] = miss.
        raptor::core::u64 raygenOffset = 0;
        raptor::core::u64 hitOffset    = static_cast<raptor::core::u64>(1) * sbtAlignedStride_;
        raptor::core::u64 missOffset   = static_cast<raptor::core::u64>(2) * sbtAlignedStride_;
        raptor::core::u64 stride       = static_cast<raptor::core::u64>(sbtAlignedStride_);

        rtEnc->traceRays(
            sbtBuffer_, raygenOffset, stride,
            sbtBuffer_, missOffset, stride,
            sbtBuffer_, hitOffset, stride,
            width_, height_);
    }

    // ---- Transition: output texture ShaderWrite -> CopySrc, swapchain Present -> CopyDst ----
    {
        dr::TextureBarrier texBarriers[2]{};
        texBarriers[0].texture  = outputTexture_;
        texBarriers[0].oldState = dr::ResourceState::ShaderWrite;
        texBarriers[0].newState = dr::ResourceState::CopySrc;

        texBarriers[1].texture  = swapChain_->currentTexture();
        texBarriers[1].oldState = dr::ResourceState::Present;
        texBarriers[1].newState = dr::ResourceState::CopyDst;

        dr::BarrierGroup bg{};
        bg.textureBarriers = Span<const dr::TextureBarrier>(texBarriers, 2);
        enc->barrier(bg);
    }

    // ---- Copy RT output to swapchain ----
    outputTextureState_ = dr::ResourceState::CopySrc;
    {
        dr::TextureCopyRegion region{};
        region.extent = dr::Extent3D{ width_, height_, 1 };
        enc->copyTextureToTexture(outputTexture_, swapChain_->currentTexture(), region);
    }

    // ---- Transition swapchain CopyDst -> Present ----
    enc->transitionTexture(swapChain_->currentTexture(),
                           dr::ResourceState::CopyDst, dr::ResourceState::Present);

    // Finish and submit.
    dr::CommandBuffer* cb = enc->finish();
    fenceVal_++;
    dr::CommandBuffer* cbs[1] = { cb };
    graphicsQueue_->submit(Span<dr::CommandBuffer* const>(cbs, 1), fence_, fenceVal_);

    // Present.
    swapChain_->present(graphicsQueue_);
    pool_->destroyEncoder(enc);
}

void RayTracingSample::onResize(raptor::core::u32 w, raptor::core::u32 h) {
    using raptor::core::Status, raptor::core::Span;
    // Wait for GPU idle before destroying resources.
    if (fence_) fence_->wait(fenceVal_, ~0ull);

    // Destroy old bind group, view, and texture.
    if (rtBindGroup_)      device_->destroyBindGroup(rtBindGroup_);
    if (outputTextureView_) device_->destroyTextureView(outputTextureView_);
    if (outputTexture_)     device_->destroyTexture(outputTexture_);
    rtBindGroup_ = nullptr; outputTextureView_ = nullptr; outputTexture_ = nullptr;

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
    td.label           = u"RTOutputTex";
    device_->createTexture(td, outputTexture_);

    dr::TextureViewDesc tvd{}; tvd.label = u"RTOutputView";
    device_->createTextureView(outputTexture_, tvd, outputTextureView_);

    // Recreate bind group with new texture view + same TLAS.
    dr::BindGroupEntry bgEntries[2]{};
    bgEntries[0] = dr::BindGroupEntry::textureEntry(outputTextureView_);
    bgEntries[1] = dr::BindGroupEntry::accelStructEntry(tlas_);
    dr::BindGroupDesc bgd{};
    bgd.layout  = rtBindGroupLayout_;
    bgd.entries = Span<const dr::BindGroupEntry>(bgEntries, 2);
    bgd.label   = u"RTBindGroup";
    device_->createBindGroup(bgd, rtBindGroup_);

    outputTextureState_ = dr::ResourceState::Undefined;
}

void RayTracingSample::onShutdown() {
    if (fence_) fence_->wait(fenceVal_, ~0ull);

    // RT bind group.
    if (rtBindGroup_)       device_->destroyBindGroup(rtBindGroup_);
    if (rtBindGroupLayout_) device_->destroyBindGroupLayout(rtBindGroupLayout_);

    // RT output texture.
    if (outputTextureView_) device_->destroyTextureView(outputTextureView_);
    if (outputTexture_)     device_->destroyTexture(outputTexture_);

    // RT resources.
    if (sbtBuffer_)         device_->destroyBuffer(sbtBuffer_);
    if (rtPipeline_)        device_->destroyRayTracingPipeline(rtPipeline_);
    if (rtPipelineLayout_)  device_->destroyPipelineLayout(rtPipelineLayout_);
    if (instanceBuffer_)    device_->destroyBuffer(instanceBuffer_);
    if (scratchBuffer_)     device_->destroyBuffer(scratchBuffer_);
    if (tlas_)              device_->destroyAccelStruct(tlas_);
    if (blas_)              device_->destroyAccelStruct(blas_);
    if (rtVertexBuffer_)    device_->destroyBuffer(rtVertexBuffer_);
    if (rtShaderModule_)    device_->destroyShaderModule(rtShaderModule_);

    if (fence_) device_->destroyFence(fence_);
    if (pool_)  device_->destroyCommandPool(pool_);

    if (compiler_) { compiler_->destroy(); delete compiler_; }
}

int main(int argc, char** argv) { RayTracingSample app; return app.run(argc, argv); }
