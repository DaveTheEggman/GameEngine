/// Vulkan implementation of Device.
/// Ported from Sedulous.RHI.Vulkan/VulkanDevice.bf.

module;
#include "Core/Prelude.h"

#include "VkIncludes.h"

#include <cstdio>
#include <cstring>

export module raptor.rhi.vk:device;

import raptor.core;
import raptor.rhi;
import :adapter;
import :surface;
import :buffer;
import :texture;
import :texture_view;
import :sampler;
import :shader_module;
import :fence;
import :query_set;
import :binding_shifts;
import :bind_group_layout;
import :bind_group;
import :pipeline_layout;
import :pipeline_cache;
import :render_pipeline;
import :compute_pipeline;
import :mesh_pipeline;
import :accel_struct;
import :ray_tracing_pipeline;
import :command_pool;
import :command_buffer;
import :queue;
import :swap_chain;
import :descriptor_pool_manager;
import :conversions;

using namespace raptor::core;

export namespace raptor::rhi::vk {

class VkDeviceImpl : public Device {
public:
    Status init(VkAdapterImpl* adapter, const DeviceDesc& desc) {
        adapter_ = adapter;
        features = adapter->buildFeatures();

        bool bindlessEnabled = desc.requiredFeatures.bindlessDescriptors && adapter->supportsDescriptorIndexing();
        bool meshEnabled     = desc.requiredFeatures.meshShaders         && adapter->supportsMeshShader();
        bool rtEnabled       = desc.requiredFeatures.rayTracing          && adapter->supportsRayTracing();
        meshEnabled_      = meshEnabled;
        rtEnabled_        = rtEnabled;
        bindlessEnabled_  = bindlessEnabled;
        // Validation flag: set externally by Adapter.createDevice after init.

        // Find queue families.
        i32 gfxFamily  = adapter->findQueueFamily(QueueType::Graphics);
        i32 compFamily = adapter->findQueueFamily(QueueType::Compute);
        i32 xferFamily = adapter->findQueueFamily(QueueType::Transfer);
        if (gfxFamily < 0) return ErrorCode::Unknown;

        // Build queue create infos.
        struct FamilyRequest { u32 family; u32 count; };
        Array<FamilyRequest> familyReqs;
        auto addFamily = [&](i32 f, u32 requested) {
            if (f < 0 || requested == 0) return;
            u32 avail = adapter->queueFamilies()[f].queueCount;
            for (auto& fr : familyReqs) { if (fr.family == static_cast<u32>(f)) { fr.count = Min(fr.count + requested, avail); return; } }
            familyReqs.PushBack({ static_cast<u32>(f), Min(requested, avail) });
        };
        addFamily(gfxFamily,  desc.graphicsQueueCount);
        addFamily(compFamily, desc.computeQueueCount);
        addFamily(xferFamily, desc.transferQueueCount);

        Array<VkDeviceQueueCreateInfo> queueCis;
        Array<Array<f32>> priorities;
        for (auto& fr : familyReqs) {
            priorities.EmplaceBack(fr.count, 1.0f);
            VkDeviceQueueCreateInfo qci{}; qci.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
            qci.queueFamilyIndex = fr.family; qci.queueCount = fr.count;
            qci.pQueuePriorities = priorities.Back().Data();
            queueCis.PushBack(qci);
        }

        // Extensions.
        Array<const char*> exts;
        exts.PushBack(VK_KHR_SWAPCHAIN_EXTENSION_NAME);
        if (meshEnabled) exts.PushBack("VK_EXT_mesh_shader");
        if (rtEnabled) {
            exts.PushBack("VK_KHR_ray_tracing_pipeline");
            exts.PushBack("VK_KHR_acceleration_structure");
            exts.PushBack("VK_KHR_deferred_host_operations");
            exts.PushBack("VK_KHR_ray_query");
        }

        // Feature chain.
        VkPhysicalDeviceVulkan13Features features13{}; features13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
        features13.dynamicRendering = VK_TRUE; features13.synchronization2 = VK_TRUE;
        features13.shaderDemoteToHelperInvocation = VK_TRUE;

        VkPhysicalDeviceVulkan12Features features12{}; features12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
        features12.pNext = &features13;
        features12.timelineSemaphore = VK_TRUE;
        if (bindlessEnabled) {
            features12.descriptorIndexing                         = VK_TRUE;
            features12.descriptorBindingPartiallyBound            = VK_TRUE;
            features12.descriptorBindingVariableDescriptorCount   = VK_TRUE;
            features12.descriptorBindingSampledImageUpdateAfterBind = VK_TRUE;
            features12.descriptorBindingStorageBufferUpdateAfterBind = VK_TRUE;
            features12.runtimeDescriptorArray                    = VK_TRUE;
            features12.shaderSampledImageArrayNonUniformIndexing = VK_TRUE;
            features12.shaderStorageBufferArrayNonUniformIndexing = VK_TRUE;
        }
        if (rtEnabled) features12.bufferDeviceAddress = VK_TRUE;

        void* featureChainTail = &features12;

        VkPhysicalDeviceMeshShaderFeaturesEXT meshFeatures{};
        meshFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MESH_SHADER_FEATURES_EXT;
        if (meshEnabled) { meshFeatures.taskShader = VK_TRUE; meshFeatures.meshShader = VK_TRUE;
            meshFeatures.pNext = featureChainTail; featureChainTail = &meshFeatures; }

        VkPhysicalDeviceRayTracingPipelineFeaturesKHR rtPipeFeatures{};
        rtPipeFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_FEATURES_KHR;
        VkPhysicalDeviceAccelerationStructureFeaturesKHR asFeatures{};
        asFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR;
        VkPhysicalDeviceRayQueryFeaturesKHR rqFeatures{};
        rqFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR;
        if (rtEnabled) {
            rtPipeFeatures.rayTracingPipeline = VK_TRUE;
            asFeatures.accelerationStructure = VK_TRUE;
            rqFeatures.rayQuery = VK_TRUE;
            rtPipeFeatures.pNext = featureChainTail; featureChainTail = &rtPipeFeatures;
            asFeatures.pNext = featureChainTail; featureChainTail = &asFeatures;
            rqFeatures.pNext = featureChainTail; featureChainTail = &rqFeatures;
        }

        VkPhysicalDeviceFeatures2 features2{}; features2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
        features2.pNext = featureChainTail;
        features2.features = adapter->features10();

        VkDeviceCreateInfo dci{}; dci.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        dci.pNext                   = &features2;
        dci.queueCreateInfoCount    = static_cast<u32>(queueCis.Size());
        dci.pQueueCreateInfos       = queueCis.Data();
        dci.enabledExtensionCount   = static_cast<u32>(exts.Size());
        dci.ppEnabledExtensionNames = exts.Data();

        if (vkCreateDevice(adapter->physicalDevice(), &dci, nullptr, &device_) != VK_SUCCESS)
            return ErrorCode::Unknown;

        type = DeviceType::Vulkan;

        // Retrieve queues.
        auto retrieveQueues = [&](i32 family, u32 count, QueueType qt, u32 offset) {
            if (family < 0 || count == 0) return;
            f32 tsPeriod = adapter->properties().limits.timestampPeriod;
            for (u32 i = 0; i < count; ++i) {
                VkQueue q = VK_NULL_HANDLE;
                vkGetDeviceQueue(device_, static_cast<u32>(family), offset + i, &q);
                auto* vkQ = new VkQueueImpl(q, qt, static_cast<u32>(family), tsPeriod, this, device_, adapter->physicalDevice());
                allQueues_.PushBack(vkQ);
                switch (qt) {
                case QueueType::Graphics: gfxQueues_.PushBack(vkQ); break;
                case QueueType::Compute:  compQueues_.PushBack(vkQ); break;
                case QueueType::Transfer: xferQueues_.PushBack(vkQ); break;
                }
            }
        };
        // Track how many queues have been claimed from each family to avoid overrun.
        auto familyAvail = [&](i32 family) -> u32 {
            u32 total = adapter->queueFamilies()[family].queueCount;
            for (auto& fr : familyReqs) if (fr.family == static_cast<u32>(family)) return fr.count;
            return total;
        };

        u32 gfxCount = Min(desc.graphicsQueueCount, familyAvail(gfxFamily));
        retrieveQueues(gfxFamily, gfxCount, QueueType::Graphics, 0);

        if (compFamily >= 0) {
            u32 offset = (compFamily == gfxFamily) ? gfxCount : 0;
            u32 avail  = adapter->queueFamilies()[compFamily].queueCount - offset;
            u32 compCount = Min(desc.computeQueueCount, avail);
            retrieveQueues(compFamily, compCount, QueueType::Compute, offset);
        }
        if (xferFamily >= 0) {
            u32 offset = 0;
            if (xferFamily == gfxFamily) offset = gfxCount + static_cast<u32>(compQueues_.Size());
            else if (xferFamily == compFamily) offset = static_cast<u32>(compQueues_.Size());
            u32 avail = adapter->queueFamilies()[xferFamily].queueCount - offset;
            u32 xferCount = Min(desc.transferQueueCount, avail);
            retrieveQueues(xferFamily, xferCount, QueueType::Transfer, offset);
        }

        // Descriptor pool manager.
        poolManager_ = new VkDescriptorPoolManager(device_, 256, rtEnabled);

        // Probe depth-stencil format support. D24_S8 is optional (unsupported on AMD/RADV).
        // Configures toVkFormat() to substitute D32F/D32F_S8 when D24 variants aren't supported.
        {
            VkFormatProperties fp{};
            vkGetPhysicalDeviceFormatProperties(adapter->physicalDevice(), VK_FORMAT_D24_UNORM_S8_UINT, &fp);
            bool d24s8 = (fp.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) != 0;
            vkGetPhysicalDeviceFormatProperties(adapter->physicalDevice(), VK_FORMAT_X8_D24_UNORM_PACK32, &fp);
            bool d24 = (fp.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) != 0;
            setDepthFormatSupport(d24s8, d24);
        }

        // RT properties.
        if (rtEnabled) {
            VkPhysicalDeviceRayTracingPipelinePropertiesKHR rtProps{};
            rtProps.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_PROPERTIES_KHR;
            VkPhysicalDeviceProperties2 p2{}; p2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2; p2.pNext = &rtProps;
            vkGetPhysicalDeviceProperties2(adapter->physicalDevice(), &p2);
            shaderGroupHandleSize      = rtProps.shaderGroupHandleSize;
            shaderGroupHandleAlignment = rtProps.shaderGroupHandleAlignment;
            shaderGroupBaseAlignment   = rtProps.shaderGroupBaseAlignment;
        }

        return ErrorCode::Ok;
    }

    // ---- Device interface ----

    Queue* getQueue(QueueType t, u32 index) override {
        switch (t) {
        case QueueType::Graphics: return index < gfxQueues_.Size()  ? gfxQueues_[index]  : nullptr;
        case QueueType::Compute:  return index < compQueues_.Size() ? compQueues_[index] : nullptr;
        case QueueType::Transfer: return index < xferQueues_.Size() ? xferQueues_[index] : nullptr;
        } return nullptr;
    }
    u32 getQueueCount(QueueType t) override {
        switch (t) {
        case QueueType::Graphics: return static_cast<u32>(gfxQueues_.Size());
        case QueueType::Compute:  return static_cast<u32>(compQueues_.Size());
        case QueueType::Transfer: return static_cast<u32>(xferQueues_.Size());
        } return 0;
    }

    FormatSupport getFormatSupport(TextureFormat format) override {
        VkFormatProperties fp{};
        vkGetPhysicalDeviceFormatProperties(adapter_->physicalDevice(), toVkFormat(format), &fp);
        auto opt = fp.optimalTilingFeatures;
        auto buf = fp.bufferFeatures;
        FormatSupport s = FormatSupport::Unsupported;
        if (opt & VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT)              s = s | FormatSupport::Texture;
        if (opt & VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT)              s = s | FormatSupport::StorageTexture;
        if (opt & VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT)           s = s | FormatSupport::ColorAttachment;
        if (opt & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT)   s = s | FormatSupport::DepthStencil;
        if (opt & VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BLEND_BIT)     s = s | FormatSupport::BlendableColor;
        if (opt & VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT)s = s | FormatSupport::LinearFilter;
        if (buf & VK_FORMAT_FEATURE_UNIFORM_TEXEL_BUFFER_BIT)       s = s | FormatSupport::Buffer;
        if (buf & VK_FORMAT_FEATURE_STORAGE_TEXEL_BUFFER_BIT)       s = s | FormatSupport::StorageBuffer;
        if (buf & VK_FORMAT_FEATURE_VERTEX_BUFFER_BIT)              s = s | FormatSupport::VertexBuffer;
        return s;
    }

    // ---- Resource creation ----
    Status createBuffer(const BufferDesc& d, Buffer*& out) override {
        auto* b = new VkBufferImpl(); if (b->init(device_, adapter_, d) != ErrorCode::Ok) { delete b; out = nullptr; return ErrorCode::Unknown; }
        out = b; return ErrorCode::Ok;
    }
    Status createTexture(const TextureDesc& d, Texture*& out) override {
        auto* t = new VkTextureImpl(); if (t->init(device_, adapter_, d) != ErrorCode::Ok) { delete t; out = nullptr; return ErrorCode::Unknown; }
        out = t; return ErrorCode::Ok;
    }
    Status createTextureView(Texture* tex, const TextureViewDesc& d, TextureView*& out) override {
        auto* v = new VkTextureViewImpl(); if (v->init(device_, static_cast<VkTextureImpl*>(tex), d) != ErrorCode::Ok) { delete v; out = nullptr; return ErrorCode::Unknown; }
        out = v; return ErrorCode::Ok;
    }
    Status createSampler(const SamplerDesc& d, Sampler*& out) override {
        auto* s = new VkSamplerImpl(); if (s->init(device_, d) != ErrorCode::Ok) { delete s; out = nullptr; return ErrorCode::Unknown; }
        out = s; return ErrorCode::Ok;
    }
    Status createShaderModule(const ShaderModuleDesc& d, ShaderModule*& out) override {
        auto* m = new VkShaderModuleImpl(); if (m->init(device_, d) != ErrorCode::Ok) { delete m; out = nullptr; return ErrorCode::Unknown; }
        out = m; return ErrorCode::Ok;
    }
    Status createBindGroupLayout(const BindGroupLayoutDesc& d, BindGroupLayout*& out) override {
        auto* l = new VkBindGroupLayoutImpl(); if (l->init(device_, d, bindingShifts_) != ErrorCode::Ok) { delete l; out = nullptr; return ErrorCode::Unknown; }
        out = l; return ErrorCode::Ok;
    }
    Status createBindGroup(const BindGroupDesc& d, BindGroup*& out) override {
        auto* g = new VkBindGroupImpl(); if (g->init(device_, poolManager_, d, bindingShifts_) != ErrorCode::Ok) { delete g; out = nullptr; return ErrorCode::Unknown; }
        out = g; return ErrorCode::Ok;
    }
    Status createPipelineLayout(const PipelineLayoutDesc& d, PipelineLayout*& out) override {
        auto* l = new VkPipelineLayoutImpl(); if (l->init(device_, d) != ErrorCode::Ok) { delete l; out = nullptr; return ErrorCode::Unknown; }
        out = l; return ErrorCode::Ok;
    }
    Status createPipelineCache(const PipelineCacheDesc& d, PipelineCache*& out) override {
        auto* c = new VkPipelineCacheImpl(); if (c->init(device_, d) != ErrorCode::Ok) { delete c; out = nullptr; return ErrorCode::Unknown; }
        out = c; return ErrorCode::Ok;
    }
    Status createRenderPipeline(const RenderPipelineDesc& d, RenderPipeline*& out) override {
        auto* p = new VkRenderPipelineImpl(); if (p->init(device_, d) != ErrorCode::Ok) { delete p; out = nullptr; return ErrorCode::Unknown; }
        out = p; return ErrorCode::Ok;
    }
    Status createComputePipeline(const ComputePipelineDesc& d, ComputePipeline*& out) override {
        auto* p = new VkComputePipelineImpl(); if (p->init(device_, d) != ErrorCode::Ok) { delete p; out = nullptr; return ErrorCode::Unknown; }
        out = p; return ErrorCode::Ok;
    }
    Status createCommandPool(QueueType qt, CommandPool*& out) override {
        auto* p = new VkCommandPoolImpl(); p->ownerDevice = this;
        if (p->init(device_, adapter_, qt) != ErrorCode::Ok) { delete p; out = nullptr; return ErrorCode::Unknown; }
        out = p; return ErrorCode::Ok;
    }
    Status createFence(u64 initialValue, Fence*& out) override {
        auto* f = new VkFenceImpl(); if (f->init(device_, initialValue) != ErrorCode::Ok) { delete f; out = nullptr; return ErrorCode::Unknown; }
        out = f; return ErrorCode::Ok;
    }
    Status createQuerySet(const QuerySetDesc& d, QuerySet*& out) override {
        auto* q = new VkQuerySetImpl(); if (q->init(device_, d) != ErrorCode::Ok) { delete q; out = nullptr; return ErrorCode::Unknown; }
        out = q; return ErrorCode::Ok;
    }
    Status createSwapChain(Surface* surface, const SwapChainDesc& d, SwapChain*& out) override {
        auto* sc = new VkSwapChainImpl();
        auto* vkSurf = static_cast<VkSurfaceImpl*>(surface);
        if (sc->init(device_, adapter_->physicalDevice(), vkSurf->handle(), d, this) != ErrorCode::Ok)
        { delete sc; out = nullptr; return ErrorCode::Unknown; }
        out = sc; return ErrorCode::Ok;
    }

    // ---- Mesh shader (folded in) ----
    Status createMeshPipeline(const MeshPipelineDesc& d, MeshPipeline*& out) override {
        if (!meshEnabled_) { out = nullptr; return ErrorCode::NotSupported; }
        auto* p = new VkMeshPipelineImpl(); if (p->init(device_, d) != ErrorCode::Ok) { delete p; out = nullptr; return ErrorCode::Unknown; }
        out = p; return ErrorCode::Ok;
    }
    void destroyMeshPipeline(MeshPipeline*& p) override {
        if (p) { static_cast<VkMeshPipelineImpl*>(p)->cleanup(device_); delete p; p = nullptr; }
    }

    // ---- Ray tracing (folded in) ----
    Status createAccelStruct(const AccelStructDesc& d, AccelStruct*& out) override {
        if (!rtEnabled_) { out = nullptr; return ErrorCode::NotSupported; }
        auto* a = new VkAccelStructImpl(); if (a->init(device_, adapter_, d, 256 * 1024) != ErrorCode::Ok) { delete a; out = nullptr; return ErrorCode::Unknown; }
        out = a; return ErrorCode::Ok;
    }
    void destroyAccelStruct(AccelStruct*& a) override {
        if (a) { static_cast<VkAccelStructImpl*>(a)->cleanup(device_); delete a; a = nullptr; }
    }
    Status createRayTracingPipeline(const RayTracingPipelineDesc& d, RayTracingPipeline*& out) override {
        if (!rtEnabled_) { out = nullptr; return ErrorCode::NotSupported; }
        auto* p = new VkRayTracingPipelineImpl(); if (p->init(device_, d) != ErrorCode::Ok) { delete p; out = nullptr; return ErrorCode::Unknown; }
        out = p; return ErrorCode::Ok;
    }
    void destroyRayTracingPipeline(RayTracingPipeline*& p) override {
        if (p) { static_cast<VkRayTracingPipelineImpl*>(p)->cleanup(device_); delete p; p = nullptr; }
    }
    Status getShaderGroupHandles(RayTracingPipeline* pipeline, u32 firstGroup, u32 groupCount, Span<u8> outData) override {
        if (!rtEnabled_) return ErrorCode::NotSupported;
        auto* p = static_cast<VkRayTracingPipelineImpl*>(pipeline);
        auto pfn = reinterpret_cast<PFN_vkGetRayTracingShaderGroupHandlesKHR>(
            vkGetDeviceProcAddr(device_, "vkGetRayTracingShaderGroupHandlesKHR"));
        if (!pfn) return ErrorCode::Unknown;
        return pfn(device_, p->handle(), firstGroup, groupCount, outData.Size(), outData.Data()) == VK_SUCCESS
            ? ErrorCode::Ok : ErrorCode::Unknown;
    }

    // ---- Resource destruction ----
    void destroyBuffer(Buffer*& b)              override { if (b) { static_cast<VkBufferImpl*>(b)->cleanup(device_); delete b; b = nullptr; } }
    void destroyTexture(Texture*& t)            override { if (t) { static_cast<VkTextureImpl*>(t)->cleanup(device_); delete t; t = nullptr; } }
    void destroyTextureView(TextureView*& v)    override { if (v) { static_cast<VkTextureViewImpl*>(v)->cleanup(device_); delete v; v = nullptr; } }
    void destroySampler(Sampler*& s)            override { if (s) { static_cast<VkSamplerImpl*>(s)->cleanup(device_); delete s; s = nullptr; } }
    void destroyShaderModule(ShaderModule*& m)  override { if (m) { static_cast<VkShaderModuleImpl*>(m)->cleanup(device_); delete m; m = nullptr; } }
    void destroyBindGroupLayout(BindGroupLayout*& l) override { if (l) { static_cast<VkBindGroupLayoutImpl*>(l)->cleanup(device_); delete l; l = nullptr; } }
    void destroyBindGroup(BindGroup*& g)        override { if (g) { static_cast<VkBindGroupImpl*>(g)->cleanup(device_, poolManager_); delete g; g = nullptr; } }
    void destroyPipelineLayout(PipelineLayout*& l) override { if (l) { static_cast<VkPipelineLayoutImpl*>(l)->cleanup(device_); delete l; l = nullptr; } }
    void destroyPipelineCache(PipelineCache*& c) override { if (c) { static_cast<VkPipelineCacheImpl*>(c)->cleanup(device_); delete c; c = nullptr; } }
    void destroyRenderPipeline(RenderPipeline*& p) override { if (p) { static_cast<VkRenderPipelineImpl*>(p)->cleanup(device_); delete p; p = nullptr; } }
    void destroyComputePipeline(ComputePipeline*& p) override { if (p) { static_cast<VkComputePipelineImpl*>(p)->cleanup(device_); delete p; p = nullptr; } }
    void destroyCommandPool(CommandPool*& p)    override { if (p) { static_cast<VkCommandPoolImpl*>(p)->cleanup(); delete p; p = nullptr; } }
    void destroyFence(Fence*& f)                override { if (f) { static_cast<VkFenceImpl*>(f)->cleanup(device_); delete f; f = nullptr; } }
    void destroyQuerySet(QuerySet*& q)          override { if (q) { static_cast<VkQuerySetImpl*>(q)->cleanup(device_); delete q; q = nullptr; } }
    void destroySwapChain(SwapChain*& sc)       override { if (sc) { static_cast<VkSwapChainImpl*>(sc)->cleanup(); delete sc; sc = nullptr; } }
    void destroySurface(Surface*& s)            override { if (s) { static_cast<VkSurfaceImpl*>(s)->destroy(); delete s; s = nullptr; } }

    void waitIdle() override { vkDeviceWaitIdle(device_); }
    void destroy() override {
        waitIdle();
        if (poolManager_) { poolManager_->destroy(); delete poolManager_; poolManager_ = nullptr; }
        for (auto* q : allQueues_) delete q;
        allQueues_.Clear(); gfxQueues_.Clear(); compQueues_.Clear(); xferQueues_.Clear();
        if (device_ != VK_NULL_HANDLE) { vkDestroyDevice(device_, nullptr); device_ = VK_NULL_HANDLE; }
        delete this;
    }

    // ---- Swap chain sync ----
    void setPendingSwapChainSync(VkSemaphore acquire, VkSemaphore present) {
        pendingAcquire_ = acquire; pendingPresent_ = present; hasPendingSync_ = true;
    }
    bool consumePendingSwapChainSync(VkSemaphore& acquire, VkSemaphore& present) {
        if (!hasPendingSync_) return false;
        acquire = pendingAcquire_; present = pendingPresent_; hasPendingSync_ = false; return true;
    }

    [[nodiscard]] VkDevice       handle()  const { return device_; }
    [[nodiscard]] VkAdapterImpl* adapter() const { return adapter_; }
    [[nodiscard]] bool           validationEnabled() const { return validationEnabled_; }

    [[nodiscard]] const BindingShifts& bindingShifts() const { return bindingShifts_; }
    void setBindingShifts(const BindingShifts& s) { bindingShifts_ = s; }

    /// Set a Vulkan debug name on an object (only when validation is enabled).
    void setDebugName(VkObjectType objectType, u64 objectHandle, StringView name) {
        if (!validationEnabled_ || name.IsEmpty()) return;
        auto pfn = reinterpret_cast<PFN_vkSetDebugUtilsObjectNameEXT>(
            vkGetDeviceProcAddr(device_, "vkSetDebugUtilsObjectNameEXT"));
        if (!pfn) return;
        char buf[256]{};
        auto len = Min(name.Size(), static_cast<usize>(255));
        std::memcpy(buf, name.Data(), len);
        VkDebugUtilsObjectNameInfoEXT ni{};
        ni.sType        = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT;
        ni.objectType   = objectType;
        ni.objectHandle = objectHandle;
        ni.pObjectName  = buf;
        pfn(device_, &ni);
    }

private:
    VkDevice         device_  = VK_NULL_HANDLE;
    VkAdapterImpl*   adapter_ = nullptr;
    bool             meshEnabled_      = false;
    bool             rtEnabled_        = false;
    bool             bindlessEnabled_  = false;
    bool             validationEnabled_= false;
    BindingShifts    bindingShifts_ = BindingShifts::standard();
    VkDescriptorPoolManager* poolManager_ = nullptr;

    Array<VkQueueImpl*> allQueues_, gfxQueues_, compQueues_, xferQueues_;

    VkSemaphore pendingAcquire_ = VK_NULL_HANDLE;
    VkSemaphore pendingPresent_ = VK_NULL_HANDLE;
    bool        hasPendingSync_ = false;
};

// ---- Adapter::createDevice implementation ----

Status VkAdapterImpl::createDevice(const DeviceDesc& desc, Device*& out) {
    auto* dev = new VkDeviceImpl();
    if (dev->init(this, desc) != ErrorCode::Ok) { delete dev; out = nullptr; return ErrorCode::Unknown; }
    out = dev; return ErrorCode::Ok;
}

// ---- SwapChain acquire/present implementations ----

Status VkSwapChainImpl::acquireNextImage() {
    VkSemaphore acquireSem = acquireSems_[frameIndex_];
    u32 imgIdx = 0;
    VkResult vr = vkAcquireNextImageKHR(device_, swapchain_, ~0ull, acquireSem, VK_NULL_HANDLE, &imgIdx);
    currentImageIndex_ = imgIdx;
    if (vr == VK_ERROR_OUT_OF_DATE_KHR) return ErrorCode::Unknown;
    if (vr != VK_SUCCESS && vr != VK_SUBOPTIMAL_KHR) return ErrorCode::Unknown;
    VkSemaphore presentSem = presentSems_[currentImageIndex_];
    owner_->setPendingSwapChainSync(acquireSem, presentSem);
    return ErrorCode::Ok;
}

Status VkSwapChainImpl::present(Queue* queue) {
    auto* vkQ = static_cast<VkQueueImpl*>(queue);
    VkSemaphore waitSem = presentSems_[currentImageIndex_];
    VkPresentInfoKHR pi{}; pi.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    pi.waitSemaphoreCount = 1; pi.pWaitSemaphores = &waitSem;
    pi.swapchainCount = 1; pi.pSwapchains = &swapchain_; pi.pImageIndices = &currentImageIndex_;
    VkResult vr = vkQueuePresentKHR(vkQ->handle(), &pi);
    frameIndex_ = (frameIndex_ + 1) % bufferCount_;
    if (vr == VK_ERROR_OUT_OF_DATE_KHR || vr == VK_SUBOPTIMAL_KHR) return ErrorCode::Unknown;
    return vr == VK_SUCCESS ? ErrorCode::Ok : ErrorCode::Unknown;
}

// ---- Queue submit-with-fence (needs Device for swap chain sync) ----

void VkQueueImpl::submit(Span<CommandBuffer* const> cmdBufs, Fence* signalFence, u64 signalValue) {
    if (cmdBufs.Size() == 0) return;
    Array<VkCommandBuffer> bufs(cmdBufs.Size());
    for (usize i = 0; i < cmdBufs.Size(); ++i)
        bufs[i] = static_cast<VkCommandBufferImpl*>(cmdBufs[i])->handle();

    auto* vkFence = static_cast<VkFenceImpl*>(signalFence);
    if (!vkFence) return;

    VkSemaphore acquireSem = VK_NULL_HANDLE, presentSem = VK_NULL_HANDLE;
    bool hasSync = device_->consumePendingSwapChainSync(acquireSem, presentSem);

    VkSemaphore waitSems[1]  = { acquireSem };
    VkPipelineStageFlags waitStages[1] = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };
    u64 waitValues[1] = { 0 };

    auto timelineSem = vkFence->handle();
    VkSemaphore signalSems[2] = { timelineSem, presentSem };
    u64 signalValues[2] = { signalValue, 0 };

    VkTimelineSemaphoreSubmitInfo tsi{}; tsi.sType = VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO;
    tsi.signalSemaphoreValueCount = hasSync ? 2u : 1u; tsi.pSignalSemaphoreValues = signalValues;
    tsi.waitSemaphoreValueCount   = hasSync ? 1u : 0u; tsi.pWaitSemaphoreValues   = waitValues;

    VkSubmitInfo si{}; si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO; si.pNext = &tsi;
    si.commandBufferCount   = static_cast<u32>(bufs.Size()); si.pCommandBuffers = bufs.Data();
    si.signalSemaphoreCount = hasSync ? 2u : 1u;             si.pSignalSemaphores = signalSems;
    if (hasSync) { si.waitSemaphoreCount = 1; si.pWaitSemaphores = waitSems; si.pWaitDstStageMask = waitStages; }

    vkQueueSubmit(queue_, 1, &si, VK_NULL_HANDLE);
}

void VkQueueImpl::submit(Span<CommandBuffer* const> cmdBufs,
                         Span<Fence* const> waitFences, Span<const u64> waitVals,
                         Fence* signalFence, u64 signalValue) {
    if (cmdBufs.Size() == 0) return;
    Array<VkCommandBuffer> bufs(cmdBufs.Size());
    for (usize i = 0; i < cmdBufs.Size(); ++i)
        bufs[i] = static_cast<VkCommandBufferImpl*>(cmdBufs[i])->handle();

    // 5-arg submit does NOT consume swap chain sync — matching Sedulous.
    // The 2-arg submit (used by the first queue submit each frame) handles it.
    Array<VkSemaphore>          waitSems(waitFences.Size());
    Array<VkPipelineStageFlags> waitStages(waitFences.Size());
    for (usize i = 0; i < waitFences.Size(); ++i) {
        waitSems[i]   = static_cast<VkFenceImpl*>(waitFences[i])->handle();
        waitStages[i] = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
    }

    VkTimelineSemaphoreSubmitInfo tsi{};
    tsi.sType = VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO;
    tsi.waitSemaphoreValueCount = static_cast<u32>(waitVals.Size());
    tsi.pWaitSemaphoreValues    = waitVals.Data();

    VkSubmitInfo si{}; si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO; si.pNext = &tsi;
    si.commandBufferCount   = static_cast<u32>(bufs.Size());  si.pCommandBuffers  = bufs.Data();
    si.waitSemaphoreCount   = static_cast<u32>(waitSems.Size()); si.pWaitSemaphores = waitSems.Data();
    si.pWaitDstStageMask    = waitStages.Data();

    VkSemaphore signalSem = VK_NULL_HANDLE;
    if (signalFence) {
        signalSem = static_cast<VkFenceImpl*>(signalFence)->handle();
        tsi.signalSemaphoreValueCount = 1;
        tsi.pSignalSemaphoreValues    = &signalValue;
        si.signalSemaphoreCount       = 1;
        si.pSignalSemaphores          = &signalSem;
    }

    vkQueueSubmit(queue_, 1, &si, VK_NULL_HANDLE);
}

} // namespace raptor::rhi::vk
