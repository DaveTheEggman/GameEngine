#include <new>
// RHI Smoketest — low-level API tour exercising the VK backend directly.
// No framework dependency; useful for debugging the RHI itself.

#include <cstdio>
#include <cstdint>
#include <cstring>

import draconic.core;
import draconic.rhi;
import draconic.rhi.vk;
import draconic.rhi.null;
import draconic.rhi.validation;
import draconic.shell;
import draconic.shell.desktop;
#ifdef DRACONIC_HAS_SHADERS
import draconic.shaders;
#endif
#ifdef DRACONIC_HAS_DX12
import draconic.rhi.dx12;
#endif

static const char* adapterTypeStr(draconic::rhi::AdapterType t) {
    using draconic::rhi::AdapterType;
    switch (t) {
    case AdapterType::DiscreteGpu:   return "DiscreteGpu";
    case AdapterType::IntegratedGpu: return "IntegratedGpu";
    case AdapterType::Cpu:           return "Cpu";
    default:                         return "Unknown";
    }
}

int main(int /*argc*/, char** /*argv*/) {
    using namespace draconic::core;
    using namespace draconic::rhi;
    namespace vk = draconic::rhi::vk;
        namespace shl = draconic::shell;

    // ---- Shell: window via the desktop (SDL3) shell ----
    shl::WindowSettings ws{};
    ws.title  = u8"Draconic Smoketest";
    ws.width  = 1280;
    ws.height = 720;
    UniquePtr<shl::IShell> plat = shl::CreateShell(ws);
    if (!plat || plat->MainWindow() == nullptr) {
        std::fprintf(stderr, "shell/window init failed\n");
        return 1;
    }
    shl::IWindow* window = plat->MainWindow();

    const shl::NativeWindow nw = window->Native();
    void* native  = nw.window;
    void* display = nw.display;
    if (!native) {
        std::fprintf(stderr, "no native handle on shell window\n");
        return 1;
    }

    // ---- VK backend (wrapped in validation layer) ----
    vk::VkBackendDesc vkDesc{ .enableValidation = true };
    Backend* rawBackend = nullptr;
    if (vk::CreateBackend(vkDesc, rawBackend) != draconic::core::ErrorCode::Ok) {
        std::fprintf(stderr, "createBackend failed\n");
        return 1;
    }
    Backend* backend = validation::CreateValidatedBackend(rawBackend);

    auto adapters = backend->EnumerateAdapters();
    if (adapters.Size() == 0) { backend->Destroy(); return 1; }

    // Adapters are enumerated best-GPU-first (see Backend::enumerateAdapters).
    Adapter* chosen = adapters[0];
    auto adapterInfo = chosen->Info();
    const String adapterName = String(adapterInfo.name);
    std::printf("adapter: %s (%s)\n", reinterpret_cast<const char*>(adapterName.CStr()), adapterTypeStr(adapterInfo.type));

    DeviceDesc dd{};
    dd.graphicsQueueCount = 1;
    dd.computeQueueCount  = 1;
    dd.transferQueueCount = 1;
    dd.requiredFeatures.meshShaders = adapterInfo.supportedFeatures.meshShaders;
    Device* device = nullptr;
    if (chosen->CreateDevice(dd, device) != draconic::core::ErrorCode::Ok) {
        std::fprintf(stderr, "createDevice failed\n");
        backend->Destroy(); return 1;
    }

    // ---- Surface + swap chain ----
    Surface* surface = nullptr;
    if (backend->CreateSurface(native, display, surface) != draconic::core::ErrorCode::Ok) {
        std::fprintf(stderr, "createSurface failed\n");
        device->Destroy(); return 1;
    }
    std::printf("surface created\n");

    SwapChainDesc sd{};
    sd.width  = 1280; sd.height = 720;
    sd.format = TextureFormat::BGRA8UnormSrgb;
    sd.presentMode = PresentMode::Fifo;
    sd.bufferCount = 2;
    sd.label = u8"main";
    SwapChain* swap = nullptr;
    if (device->CreateSwapChain(surface, sd, swap) != draconic::core::ErrorCode::Ok) {
        std::fprintf(stderr, "createSwapChain failed\n");
        device->DestroySurface(surface);
        device->Destroy(); return 1;
    }
    std::printf("swap chain: %ux%u, bufferCount=%u\n", swap->Width(), swap->Height(), swap->BufferCount());

    // ---- Buffer / Sampler / ShaderModule ----
    BufferDesc ubDesc{};
    ubDesc.size  = 1024;
    ubDesc.usage = BufferUsage::Uniform | BufferUsage::CopyDst;
    ubDesc.memory = MemoryLocation::CpuToGpu;
    ubDesc.label = u8"smoketest_uniform";
    Buffer* ub = nullptr;
    if (device->CreateBuffer(ubDesc, ub) != draconic::core::ErrorCode::Ok) {
        std::fprintf(stderr, "createBuffer failed\n");
    } else {
        void* mapped = ub->Map();
        std::printf("uniform buffer: size=%llu mapped=%p\n",
                    static_cast<unsigned long long>(ub->GetSize()), mapped);
        if (mapped) std::memset(mapped, 0xAB, 16);
        ub->Unmap();
    }

    SamplerDesc sampDesc{};
    sampDesc.maxAnisotropy = 16;
    sampDesc.label = u8"smoketest_sampler";
    Sampler* samp = nullptr;
    if (device->CreateSampler(sampDesc, samp) != draconic::core::ErrorCode::Ok) {
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
    shDesc.label = u8"smoketest_noop_fs";
    ShaderModule* sh = nullptr;
    if (device->CreateShaderModule(shDesc, sh) != draconic::core::ErrorCode::Ok) {
        std::fprintf(stderr, "createShaderModule failed\n");
    } else {
        std::printf("shader module created (%zu bytes)\n", shDesc.code.Size());
        device->DestroyShaderModule(sh);
    }

    // ---- BindGroupLayout / PipelineLayout / PipelineCache ----
    BindGroupLayoutEntry layoutEntries[2] = {
        BindGroupLayoutEntry::UniformBuffer(0, ShaderStage::Vertex),
        BindGroupLayoutEntry::SampledTexture(1, ShaderStage::Fragment),
    };
    BindGroupLayoutDesc bglDesc{};
    bglDesc.entries = Span<const BindGroupLayoutEntry>(layoutEntries, 2);
    bglDesc.label = u8"smoketest_bgl";
    BindGroupLayout* bgl = nullptr;
    if (device->CreateBindGroupLayout(bglDesc, bgl) != draconic::core::ErrorCode::Ok) {
        std::fprintf(stderr, "createBindGroupLayout failed\n");
    } else {
        std::printf("bind group layout: %zu entries\n", bgl->Entries().Size());
    }

    PipelineLayoutDesc plDesc{};
    BindGroupLayout* plSets[1] = { bgl };
    plDesc.bindGroupLayouts = Span<BindGroupLayout* const>(plSets, 1);
    plDesc.label = u8"smoketest_pl";
    PipelineLayout* pl = nullptr;
    if (device->CreatePipelineLayout(plDesc, pl) != draconic::core::ErrorCode::Ok) {
        std::fprintf(stderr, "createPipelineLayout failed\n");
    } else {
        std::printf("pipeline layout created\n");
    }

    PipelineCacheDesc pcDesc{};
    pcDesc.label = u8"smoketest_pc";
    PipelineCache* pc = nullptr;
    if (device->CreatePipelineCache(pcDesc, pc) != draconic::core::ErrorCode::Ok) {
        std::fprintf(stderr, "createPipelineCache failed\n");
    } else {
        std::printf("pipeline cache created (size=%u)\n", pc->GetDataSize());
    }

    device->DestroyPipelineCache(pc);
    device->DestroyPipelineLayout(pl);
    device->DestroyBindGroupLayout(bgl);
    device->DestroySampler(samp);
    device->DestroyBuffer(ub);

    // ---- Command pool + fence ----
    CommandPool* pool = nullptr;
    if (device->CreateCommandPool(QueueType::Graphics, pool) != draconic::core::ErrorCode::Ok) {
        std::fprintf(stderr, "createCommandPool failed\n");
    } else {
        std::printf("command pool created\n");
    }

    Fence* fence = nullptr;
    if (device->CreateFence(0, fence) != draconic::core::ErrorCode::Ok) {
        std::fprintf(stderr, "createFence failed\n");
    } else {
        std::printf("fence created (initial=%llu)\n",
                    static_cast<unsigned long long>(fence->CompletedValue()));
    }

    QuerySetDesc qsDesc{};
    qsDesc.type  = QueryType::Timestamp;
    qsDesc.count = 16;
    qsDesc.label = u8"smoketest_qs";
    QuerySet* qs = nullptr;
    if (device->CreateQuerySet(qsDesc, qs) != draconic::core::ErrorCode::Ok) {
        std::fprintf(stderr, "createQuerySet failed\n");
    } else {
        std::printf("query set created (type=%u count=%u)\n",
                    static_cast<unsigned>(qs->type), qs->count);
    }

    // ---- Show window + acquire/present 3 frames ----

    Queue* gfx = device->GetQueue(QueueType::Graphics);
    u64 fenceValue = 0;
    for (int frame = 0; frame < 3; ++frame) {
        if (swap->AcquireNextImage() != draconic::core::ErrorCode::Ok) {
            std::fprintf(stderr, "acquireNextImage failed on frame %d\n", frame);
            break;
        }

        CommandEncoder* enc = nullptr;
        if (pool && pool->CreateEncoder(enc) == draconic::core::ErrorCode::Ok && enc) {
            enc->TransitionTexture(swap->CurrentTexture(), ResourceState::Undefined, ResourceState::Present);

            CommandBuffer* cb = enc->Finish();
            CommandBuffer* cbs[1] = { cb };
            fenceValue++;
            gfx->Submit(Span<CommandBuffer* const>(cbs, 1), fence, fenceValue);
            pool->DestroyEncoder(enc);
        }

        swap->Present(gfx);
        if (fence) fence->Wait(fenceValue);
        if (pool) pool->Reset();
        std::printf("frame %d acquired image_index=%u fence=%llu\n",
                    frame, swap->CurrentImageIndex(),
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
#ifdef DRACONIC_HAS_SHADERS
    {
        namespace ds = draconic::shaders;
        ds::Compiler* shaderc = nullptr;
        if (ds::createCompiler(ds::CompilerDesc{}, shaderc) != draconic::core::ErrorCode::Ok) {
            std::fprintf(stderr, "shaders: createCompiler failed\n");
        } else {
            static const char kHlsl[] =
                "float4 main(float2 uv : TEXCOORD0) : SV_Target {\n"
                "    return float4(uv, 0.0, 1.0);\n"
                "}\n";
            ds::CompileOptions opts{};
            opts.shaderModel = u8"6_0";
            opts.optimizationLevel = 3;
            ds::CompileResult cr{};
            draconic::core::Status r = shaderc->compile(
                reinterpret_cast<const u8*>(kHlsl), sizeof(kHlsl) - 1,
                ds::ShaderStage::Fragment, u8"main", ds::ShaderTarget::SPIRV, opts, cr);
            if (r == draconic::core::ErrorCode::Ok) {
                u32 magic = cr.bytecodeSize >= 4 ? *reinterpret_cast<const u32*>(cr.bytecode) : 0u;
                std::printf("HLSL->SPIR-V: %zu bytes, magic=0x%08x %s\n",
                            cr.bytecodeSize, magic, magic == 0x07230203u ? "(SPIR-V OK)" : "(unexpected)");
            } else {
                std::fprintf(stderr, "shaders: compile failed: %s\n", cr.messages ? cr.messages : "(no messages)");
            }
            shaderc->freeResult(cr);
            shaderc->Destroy();
            delete shaderc;
        }
    }
#endif

    if (qs)    device->DestroyQuerySet(qs);
    if (fence) device->DestroyFence(fence);
    if (pool)  device->DestroyCommandPool(pool);

    device->WaitIdle();
    device->DestroySwapChain(swap);
    device->DestroySurface(surface);
    device->Destroy();
    backend->Destroy();

    

    // ---- Null backend exercise ----
    std::printf("\n=== Null Backend ===\n");
    {
        namespace null = draconic::rhi::null;
        Backend* nullBackend = nullptr;
        null::CreateNullBackend(nullBackend);

        auto nullAdapters = nullBackend->EnumerateAdapters();
        std::printf("null adapters: %zu\n", nullAdapters.Size());

        Device* nullDevice = nullptr;
        nullAdapters[0]->CreateDevice(DeviceDesc{}, nullDevice);

        Surface* nullSurface = nullptr;
        nullBackend->CreateSurface(nullptr, nullSurface);

        SwapChainDesc nullSd{}; nullSd.width = 800; nullSd.height = 600; nullSd.bufferCount = 2;
        SwapChain* nullSwap = nullptr;
        nullDevice->CreateSwapChain(nullSurface, nullSd, nullSwap);
        std::printf("null swap chain: %ux%u\n", nullSwap->Width(), nullSwap->Height());

        Buffer* nullBuf = nullptr;
        BufferDesc nbd{}; nbd.size = 256; nbd.usage = BufferUsage::Uniform; nbd.memory = MemoryLocation::CpuToGpu;
        nullDevice->CreateBuffer(nbd, nullBuf);
        void* mapped = nullBuf->Map();
        std::printf("null buffer mapped: %s\n", mapped ? "yes" : "no");
        nullBuf->Unmap();

        CommandPool* nullPool = nullptr;
        nullDevice->CreateCommandPool(QueueType::Graphics, nullPool);
        CommandEncoder* nullEnc = nullptr;
        nullPool->CreateEncoder(nullEnc);
        nullSwap->AcquireNextImage();
        nullEnc->TransitionTexture(nullSwap->CurrentTexture(), ResourceState::Undefined, ResourceState::Present);
        CommandBuffer* nullCb = nullEnc->Finish();
        Fence* nullFence = nullptr;
        nullDevice->CreateFence(0, nullFence);
        CommandBuffer* nullCbs[1] = { nullCb };
        nullDevice->GetQueue(QueueType::Graphics)->Submit(Span<CommandBuffer* const>(nullCbs, 1), nullFence, 1);
        nullFence->Wait(1, ~0ull);
        nullSwap->Present(nullDevice->GetQueue(QueueType::Graphics));
        std::printf("null frame completed\n");

        nullPool->DestroyEncoder(nullEnc);
        nullDevice->DestroyFence(nullFence);
        nullDevice->DestroyCommandPool(nullPool);
        nullDevice->DestroyBuffer(nullBuf);
        nullDevice->DestroySwapChain(nullSwap);
        nullDevice->DestroySurface(nullSurface);
        nullDevice->Destroy();
        nullBackend->Destroy();
        std::printf("null backend: OK\n");
    }

    // ===== DX12 backend (Windows only) =====
#ifdef DRACONIC_HAS_DX12
    {
        namespace dx12 = draconic::rhi::dx12;
        std::printf("\n=== DX12 Backend ===\n");

        Backend* dx12Backend = nullptr;
        dx12::DxBackendDesc dx12Desc{};
        dx12Desc.enableValidation = true;
        if (dx12::CreateDxBackend(dx12Desc, dx12Backend) != ErrorCode::Ok) {
            std::printf("DX12 backend: FAILED to create\n");
        } else {
            auto dx12Adapters = dx12Backend->EnumerateAdapters();
            std::printf("DX12 adapters: %zu\n", dx12Adapters.Size());
            for (usize i = 0; i < dx12Adapters.Size(); ++i) {
                AdapterInfo ai = dx12Adapters[i]->Info();
                const String name8 = String(ai.name);
                std::printf("  [%zu] %s (%s)\n", i,
                    reinterpret_cast<const char*>(name8.CStr()),
                    adapterTypeStr(ai.type));
            }

            if (dx12Adapters.Size() > 0) {
                Device* dx12Device = nullptr;
                DeviceDesc dx12dd{}; dx12dd.graphicsQueueCount = 1;
                if (dx12Adapters[0]->CreateDevice(dx12dd, dx12Device) == ErrorCode::Ok) {
                    std::printf("DX12 device created (type=%d)\n", static_cast<int>(dx12Device->type));

                    // Create and destroy a buffer.
                    Buffer* dx12Buf = nullptr;
                    BufferDesc bd{}; bd.size = 256; bd.usage = BufferUsage::Uniform;
                    bd.memory = MemoryLocation::CpuToGpu;
                    dx12Device->CreateBuffer(bd, dx12Buf);
                    if (dx12Buf) {
                        void* mapped = dx12Buf->Map();
                        std::printf("DX12 buffer mapped: %s\n", mapped ? "OK" : "FAIL");
                        if (mapped) dx12Buf->Unmap();
                        dx12Device->DestroyBuffer(dx12Buf);
                    }

                    // Create and destroy a fence.
                    Fence* dx12Fence = nullptr;
                    dx12Device->CreateFence(0, dx12Fence);
                    if (dx12Fence) {
                        std::printf("DX12 fence completed value: %llu\n",
                            static_cast<unsigned long long>(dx12Fence->CompletedValue()));
                        dx12Device->DestroyFence(dx12Fence);
                    }

                    dx12Device->Destroy();
                }
            }

            dx12Backend->Destroy();
            std::printf("DX12 backend: OK\n");
        }
    }
#endif

    return 0;
}
