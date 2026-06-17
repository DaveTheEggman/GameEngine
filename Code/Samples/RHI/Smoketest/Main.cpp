#include <new>
// RHI Smoketest — low-level API tour exercising the VK backend directly.
// No framework dependency; useful for debugging the RHI itself.

#include <cstdio>
#include <cstdint>
#include <cstring>

import raptor.core;
import raptor.rhi;
import raptor.rhi.vk;
import raptor.rhi.null;
import raptor.rhi.validation;
import raptor.runtime.platform;
import raptor.runtime.platform.desktop;
#ifdef RAPTOR_HAS_SHADERS
import raptor.shaders;
#endif
#ifdef RAPTOR_HAS_DX12
import raptor.rhi.dx12;
#endif

static const char* adapterTypeStr(raptor::rhi::AdapterType t) {
    using raptor::rhi::AdapterType;
    switch (t) {
    case AdapterType::DiscreteGpu:   return "DiscreteGpu";
    case AdapterType::IntegratedGpu: return "IntegratedGpu";
    case AdapterType::Cpu:           return "Cpu";
    default:                         return "Unknown";
    }
}

int main(int /*argc*/, char** /*argv*/) {
    using namespace raptor::core;
    using namespace raptor::rhi;
    namespace vk = raptor::rhi::vk;
    namespace rt = raptor::runtime;

    // ---- Platform: window via the desktop (SDL3) platform ----
    rt::WindowSettings ws{};
    ws.title  = u"Raptor Smoketest";
    ws.width  = 1280;
    ws.height = 720;
    UniquePtr<rt::IPlatform> plat = rt::CreatePlatform(ws);
    if (!plat || plat->MainWindow() == nullptr) {
        std::fprintf(stderr, "platform/window init failed\n");
        return 1;
    }
    rt::IWindow* window = plat->MainWindow();

    const rt::NativeWindow nw = window->Native();
    void* native  = nw.window;
    void* display = nw.display;
    if (!native) {
        std::fprintf(stderr, "no native handle on platform window\n");
        return 1;
    }

    // ---- VK backend (wrapped in validation layer) ----
    vk::VkBackendDesc vkDesc{ .enableValidation = true };
    Backend* rawBackend = nullptr;
    if (vk::createBackend(vkDesc, rawBackend) != raptor::core::ErrorCode::Ok) {
        std::fprintf(stderr, "createBackend failed\n");
        return 1;
    }
    Backend* backend = validation::createValidatedBackend(rawBackend);

    auto adapters = backend->enumerateAdapters();
    if (adapters.Size() == 0) { backend->destroy(); return 1; }

    // Adapters are enumerated best-GPU-first (see Backend::enumerateAdapters).
    Adapter* chosen = adapters[0];
    auto adapterInfo = chosen->info();
    const UTF8String adapterName = ToUTF8(adapterInfo.name);
    std::printf("adapter: %s (%s)\n", reinterpret_cast<const char*>(adapterName.CStr()), adapterTypeStr(adapterInfo.type));

    DeviceDesc dd{};
    dd.graphicsQueueCount = 1;
    dd.computeQueueCount  = 1;
    dd.transferQueueCount = 1;
    dd.requiredFeatures.meshShaders = adapterInfo.supportedFeatures.meshShaders;
    Device* device = nullptr;
    if (chosen->createDevice(dd, device) != raptor::core::ErrorCode::Ok) {
        std::fprintf(stderr, "createDevice failed\n");
        backend->destroy(); return 1;
    }

    // ---- Surface + swap chain ----
    Surface* surface = nullptr;
    if (backend->createSurface(native, display, surface) != raptor::core::ErrorCode::Ok) {
        std::fprintf(stderr, "createSurface failed\n");
        device->destroy(); return 1;
    }
    std::printf("surface created\n");

    SwapChainDesc sd{};
    sd.width  = 1280; sd.height = 720;
    sd.format = TextureFormat::BGRA8UnormSrgb;
    sd.presentMode = PresentMode::Fifo;
    sd.bufferCount = 2;
    sd.label = u"main";
    SwapChain* swap = nullptr;
    if (device->createSwapChain(surface, sd, swap) != raptor::core::ErrorCode::Ok) {
        std::fprintf(stderr, "createSwapChain failed\n");
        device->destroySurface(surface);
        device->destroy(); return 1;
    }
    std::printf("swap chain: %ux%u, bufferCount=%u\n", swap->width(), swap->height(), swap->bufferCount());

    // ---- Buffer / Sampler / ShaderModule ----
    BufferDesc ubDesc{};
    ubDesc.size  = 1024;
    ubDesc.usage = BufferUsage::Uniform | BufferUsage::CopyDst;
    ubDesc.memory = MemoryLocation::CpuToGpu;
    ubDesc.label = u"smoketest_uniform";
    Buffer* ub = nullptr;
    if (device->createBuffer(ubDesc, ub) != raptor::core::ErrorCode::Ok) {
        std::fprintf(stderr, "createBuffer failed\n");
    } else {
        void* mapped = ub->map();
        std::printf("uniform buffer: size=%llu mapped=%p\n",
                    static_cast<unsigned long long>(ub->size()), mapped);
        if (mapped) std::memset(mapped, 0xAB, 16);
        ub->unmap();
    }

    SamplerDesc sampDesc{};
    sampDesc.maxAnisotropy = 16;
    sampDesc.label = u"smoketest_sampler";
    Sampler* samp = nullptr;
    if (device->createSampler(sampDesc, samp) != raptor::core::ErrorCode::Ok) {
        std::fprintf(stderr, "createSampler failed\n");
    } else {
        std::printf("sampler created (aniso=%u)\n", samp->desc.maxAnisotropy);
    }

    // Minimal SPIR-V noop fragment shader.
    static const u32 kSpvNoop[] = {
        0x07230203u, 0x00010000u, 0x00080001u, 0x00000005u, 0x00000000u,
        0x00020011u, 0x00000001u,
        0x0003000Eu, 0x00000000u, 0x00000001u,
        0x0005000Fu, 0x00000004u, 0x00000001u, 0x6E69616Du, 0x00000000u,
        0x00030010u, 0x00000001u, 0x00000007u,
        0x00020013u, 0x00000002u,
        0x00030021u, 0x00000003u, 0x00000002u,
        0x00050036u, 0x00000002u, 0x00000001u, 0x00000000u, 0x00000003u,
        0x000200F8u, 0x00000004u,
        0x000100FDu, 0x00010038u,
    };
    ShaderModuleDesc shDesc{};
    shDesc.code = Span<const u8>(reinterpret_cast<const u8*>(kSpvNoop), sizeof(kSpvNoop));
    shDesc.label = u"smoketest_noop_fs";
    ShaderModule* sh = nullptr;
    if (device->createShaderModule(shDesc, sh) != raptor::core::ErrorCode::Ok) {
        std::fprintf(stderr, "createShaderModule failed\n");
    } else {
        std::printf("shader module created (%zu bytes)\n", shDesc.code.Size());
        device->destroyShaderModule(sh);
    }

    // ---- BindGroupLayout / PipelineLayout / PipelineCache ----
    BindGroupLayoutEntry layoutEntries[2] = {
        BindGroupLayoutEntry::uniformBuffer(0, ShaderStage::Vertex),
        BindGroupLayoutEntry::sampledTexture(1, ShaderStage::Fragment),
    };
    BindGroupLayoutDesc bglDesc{};
    bglDesc.entries = Span<const BindGroupLayoutEntry>(layoutEntries, 2);
    bglDesc.label = u"smoketest_bgl";
    BindGroupLayout* bgl = nullptr;
    if (device->createBindGroupLayout(bglDesc, bgl) != raptor::core::ErrorCode::Ok) {
        std::fprintf(stderr, "createBindGroupLayout failed\n");
    } else {
        std::printf("bind group layout: %zu entries\n", bgl->entries().Size());
    }

    PipelineLayoutDesc plDesc{};
    BindGroupLayout* plSets[1] = { bgl };
    plDesc.bindGroupLayouts = Span<BindGroupLayout* const>(plSets, 1);
    plDesc.label = u"smoketest_pl";
    PipelineLayout* pl = nullptr;
    if (device->createPipelineLayout(plDesc, pl) != raptor::core::ErrorCode::Ok) {
        std::fprintf(stderr, "createPipelineLayout failed\n");
    } else {
        std::printf("pipeline layout created\n");
    }

    PipelineCacheDesc pcDesc{};
    pcDesc.label = u"smoketest_pc";
    PipelineCache* pc = nullptr;
    if (device->createPipelineCache(pcDesc, pc) != raptor::core::ErrorCode::Ok) {
        std::fprintf(stderr, "createPipelineCache failed\n");
    } else {
        std::printf("pipeline cache created (size=%u)\n", pc->getDataSize());
    }

    device->destroyPipelineCache(pc);
    device->destroyPipelineLayout(pl);
    device->destroyBindGroupLayout(bgl);
    device->destroySampler(samp);
    device->destroyBuffer(ub);

    // ---- Command pool + fence ----
    CommandPool* pool = nullptr;
    if (device->createCommandPool(QueueType::Graphics, pool) != raptor::core::ErrorCode::Ok) {
        std::fprintf(stderr, "createCommandPool failed\n");
    } else {
        std::printf("command pool created\n");
    }

    Fence* fence = nullptr;
    if (device->createFence(0, fence) != raptor::core::ErrorCode::Ok) {
        std::fprintf(stderr, "createFence failed\n");
    } else {
        std::printf("fence created (initial=%llu)\n",
                    static_cast<unsigned long long>(fence->completedValue()));
    }

    QuerySetDesc qsDesc{};
    qsDesc.type  = QueryType::Timestamp;
    qsDesc.count = 16;
    qsDesc.label = u"smoketest_qs";
    QuerySet* qs = nullptr;
    if (device->createQuerySet(qsDesc, qs) != raptor::core::ErrorCode::Ok) {
        std::fprintf(stderr, "createQuerySet failed\n");
    } else {
        std::printf("query set created (type=%u count=%u)\n",
                    static_cast<unsigned>(qs->type), qs->count);
    }

    // ---- Show window + acquire/present 3 frames ----

    Queue* gfx = device->getQueue(QueueType::Graphics);
    u64 fenceValue = 0;
    for (int frame = 0; frame < 3; ++frame) {
        if (swap->acquireNextImage() != raptor::core::ErrorCode::Ok) {
            std::fprintf(stderr, "acquireNextImage failed on frame %d\n", frame);
            break;
        }

        CommandEncoder* enc = nullptr;
        if (pool && pool->createEncoder(enc) == raptor::core::ErrorCode::Ok && enc) {
            enc->transitionTexture(swap->currentTexture(), ResourceState::Undefined, ResourceState::Present);

            CommandBuffer* cb = enc->finish();
            CommandBuffer* cbs[1] = { cb };
            fenceValue++;
            gfx->submit(Span<CommandBuffer* const>(cbs, 1), fence, fenceValue);
            pool->destroyEncoder(enc);
        }

        swap->present(gfx);
        if (fence) fence->wait(fenceValue);
        if (pool) pool->reset();
        std::printf("frame %d acquired image_index=%u fence=%llu\n",
                    frame, swap->currentImageIndex(),
                    static_cast<unsigned long long>(fenceValue));
    }

    // ---- Extension probes ----
    if (device->features.meshShaders) {
        std::printf("mesh shaders: supported\n");
    } else {
        std::printf("mesh shaders: not available\n");
    }
    if (device->features.rayTracing) {
        std::printf("ray tracing: supported (handle_size=%u)\n", device->shaderGroupHandleSize);
    } else {
        std::printf("ray tracing: not available\n");
    }

    // ---- Cleanup ----
    // ---- DXC shader compilation test ----
#ifdef RAPTOR_HAS_SHADERS
    {
        namespace ds = raptor::shaders;
        ds::Compiler* shaderc = nullptr;
        if (ds::createCompiler(ds::CompilerDesc{}, shaderc) != raptor::core::ErrorCode::Ok) {
            std::fprintf(stderr, "shaders: createCompiler failed\n");
        } else {
            static const char kHlsl[] =
                "float4 main(float2 uv : TEXCOORD0) : SV_Target {\n"
                "    return float4(uv, 0.0, 1.0);\n"
                "}\n";
            ds::CompileOptions opts{};
            opts.shaderModel = u"6_0";
            opts.optimizationLevel = 3;
            ds::CompileResult cr{};
            raptor::core::Status r = shaderc->compile(
                reinterpret_cast<const u8*>(kHlsl), sizeof(kHlsl) - 1,
                ds::ShaderStage::Fragment, u"main", ds::ShaderTarget::SPIRV, opts, cr);
            if (r == raptor::core::ErrorCode::Ok) {
                u32 magic = cr.bytecodeSize >= 4 ? *reinterpret_cast<const u32*>(cr.bytecode) : 0u;
                std::printf("HLSL->SPIR-V: %zu bytes, magic=0x%08x %s\n",
                            cr.bytecodeSize, magic, magic == 0x07230203u ? "(SPIR-V OK)" : "(unexpected)");
            } else {
                std::fprintf(stderr, "shaders: compile failed: %s\n", cr.messages ? cr.messages : "(no messages)");
            }
            shaderc->freeResult(cr);
            shaderc->destroy();
            delete shaderc;
        }
    }
#endif

    if (qs)    device->destroyQuerySet(qs);
    if (fence) device->destroyFence(fence);
    if (pool)  device->destroyCommandPool(pool);

    device->waitIdle();
    device->destroySwapChain(swap);
    device->destroySurface(surface);
    device->destroy();
    backend->destroy();

    

    // ---- Null backend exercise ----
    std::printf("\n=== Null Backend ===\n");
    {
        namespace null = raptor::rhi::null;
        Backend* nullBackend = nullptr;
        null::createNullBackend(nullBackend);

        auto nullAdapters = nullBackend->enumerateAdapters();
        std::printf("null adapters: %zu\n", nullAdapters.Size());

        Device* nullDevice = nullptr;
        nullAdapters[0]->createDevice(DeviceDesc{}, nullDevice);

        Surface* nullSurface = nullptr;
        nullBackend->createSurface(nullptr, nullSurface);

        SwapChainDesc nullSd{}; nullSd.width = 800; nullSd.height = 600; nullSd.bufferCount = 2;
        SwapChain* nullSwap = nullptr;
        nullDevice->createSwapChain(nullSurface, nullSd, nullSwap);
        std::printf("null swap chain: %ux%u\n", nullSwap->width(), nullSwap->height());

        Buffer* nullBuf = nullptr;
        BufferDesc nbd{}; nbd.size = 256; nbd.usage = BufferUsage::Uniform; nbd.memory = MemoryLocation::CpuToGpu;
        nullDevice->createBuffer(nbd, nullBuf);
        void* mapped = nullBuf->map();
        std::printf("null buffer mapped: %s\n", mapped ? "yes" : "no");
        nullBuf->unmap();

        CommandPool* nullPool = nullptr;
        nullDevice->createCommandPool(QueueType::Graphics, nullPool);
        CommandEncoder* nullEnc = nullptr;
        nullPool->createEncoder(nullEnc);
        nullSwap->acquireNextImage();
        nullEnc->transitionTexture(nullSwap->currentTexture(), ResourceState::Undefined, ResourceState::Present);
        CommandBuffer* nullCb = nullEnc->finish();
        Fence* nullFence = nullptr;
        nullDevice->createFence(0, nullFence);
        CommandBuffer* nullCbs[1] = { nullCb };
        nullDevice->getQueue(QueueType::Graphics)->submit(Span<CommandBuffer* const>(nullCbs, 1), nullFence, 1);
        nullFence->wait(1, ~0ull);
        nullSwap->present(nullDevice->getQueue(QueueType::Graphics));
        std::printf("null frame completed\n");

        nullPool->destroyEncoder(nullEnc);
        nullDevice->destroyFence(nullFence);
        nullDevice->destroyCommandPool(nullPool);
        nullDevice->destroyBuffer(nullBuf);
        nullDevice->destroySwapChain(nullSwap);
        nullDevice->destroySurface(nullSurface);
        nullDevice->destroy();
        nullBackend->destroy();
        std::printf("null backend: OK\n");
    }

    // ===== DX12 backend (Windows only) =====
#ifdef RAPTOR_HAS_DX12
    {
        std::printf("\n=== DX12 Backend ===\n");

        rhi::Backend* dx12Backend = nullptr;
        rhi::dx12::DxBackendDesc dx12Desc{};
        dx12Desc.enableValidation = true;
        if (rhi::dx12::createDxBackend(dx12Desc, dx12Backend) != raptor::core::ErrorCode::Ok) {
            std::printf("DX12 backend: FAILED to create\n");
        } else {
            auto dx12Adapters = dx12Backend->enumerateAdapters();
            std::printf("DX12 adapters: %zu\n", dx12Adapters.Size());
            for (usize i = 0; i < dx12Adapters.Size(); ++i) {
                rhi::AdapterInfo ai = dx12Adapters[i]->info();
                std::printf("  [%zu] %.*s (%s)\n", i,
                    static_cast<int>(ai.name.length()), ai.name.data(),
                    adapterTypeStr(ai.type));
            }

            if (dx12Adapters.Size() > 0) {
                rhi::Device* dx12Device = nullptr;
                rhi::DeviceDesc dd{}; dd.graphicsQueueCount = 1;
                if (dx12Adapters[0]->createDevice(dd, dx12Device) == raptor::core::ErrorCode::Ok) {
                    std::printf("DX12 device created (type=%d)\n", static_cast<int>(dx12Device->type));

                    // Create and destroy a buffer.
                    rhi::Buffer* dx12Buf = nullptr;
                    rhi::BufferDesc bd{}; bd.size = 256; bd.usage = rhi::BufferUsage::Uniform;
                    bd.memory = rhi::MemoryLocation::CpuToGpu;
                    dx12Device->createBuffer(bd, dx12Buf);
                    if (dx12Buf) {
                        void* mapped = dx12Buf->map();
                        std::printf("DX12 buffer mapped: %s\n", mapped ? "OK" : "FAIL");
                        if (mapped) dx12Buf->unmap();
                        dx12Device->destroyBuffer(dx12Buf);
                    }

                    // Create and destroy a fence.
                    rhi::Fence* dx12Fence = nullptr;
                    dx12Device->createFence(0, dx12Fence);
                    if (dx12Fence) {
                        std::printf("DX12 fence completed value: %llu\n",
                            static_cast<unsigned long long>(dx12Fence->completedValue()));
                        dx12Device->destroyFence(dx12Fence);
                    }

                    dx12Device->destroy();
                }
            }

            dx12Backend->destroy();
            std::printf("DX12 backend: OK\n");
        }
    }
#endif

    return 0;
}
