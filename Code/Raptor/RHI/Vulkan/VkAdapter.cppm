/// Vulkan implementation of Adapter.
/// Queries physical device properties, features, and queue families.
/// Provides feature detection and queue family selection.

module;
#include "Core/Prelude.h"

#include "VkIncludes.h"

#include <cstring>

export module raptor.rhi.vk:adapter;

import raptor.core;
import raptor.rhi;

using namespace raptor::core;

export namespace raptor::rhi::vk {

class VkDeviceImpl; // forward

class VkAdapterImpl : public Adapter {
public:
    VkAdapterImpl(VkPhysicalDevice physicalDevice, VkInstance instance)
        : physicalDevice_(physicalDevice), instance_(instance)
    {
        vkGetPhysicalDeviceProperties(physicalDevice_, &properties_);
        vkGetPhysicalDeviceFeatures(physicalDevice_, &features10_);
        vkGetPhysicalDeviceMemoryProperties(physicalDevice_, &memoryProperties_);

        u32 qfCount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice_, &qfCount, nullptr);
        queueFamilies_.Resize(qfCount);
        vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice_, &qfCount, queueFamilies_.Data());

        queryExtensionSupport();
    }

    // ---- Adapter interface ----

    void GetInfo(AdapterInfo& out) override {
        out.name = ToWide(UTF8StringView(reinterpret_cast<const utf8char*>(properties_.deviceName)));
        out.vendorId = properties_.vendorID;
        out.deviceId = properties_.deviceID;

        switch (properties_.deviceType) {
        case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU:   out.type = AdapterType::DiscreteGpu; break;
        case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU: out.type = AdapterType::IntegratedGpu; break;
        case VK_PHYSICAL_DEVICE_TYPE_CPU:            out.type = AdapterType::Cpu; break;
        default:                                     out.type = AdapterType::Unknown; break;
        }

        out.supportedFeatures = buildFeatures();
    }

    Status CreateDevice(const DeviceDesc& desc, Device*& out) override;

    // ---- Feature building ----

    DeviceFeatures buildFeatures() const {
        DeviceFeatures f{};

        f.bindlessDescriptors       = supportsDescriptorIndexing_;
        f.timestampQueries          = properties_.limits.timestampComputeAndGraphics;
        f.multiDrawIndirect         = features10_.multiDrawIndirect;
        f.depthClamp                = features10_.depthClamp;
        f.fillModeWireframe         = features10_.fillModeNonSolid;
        f.textureCompressionBC      = features10_.textureCompressionBC;
        f.textureCompressionASTC    = features10_.textureCompressionASTC_LDR;
        f.independentBlend          = features10_.independentBlend;
        f.multiViewport             = features10_.multiViewport;
        f.meshShaders               = supportsMeshShader_;
        f.rayTracing                = supportsRayTracing_;
        f.pipelineStatisticsQueries = features10_.pipelineStatisticsQuery;

        // Mesh shader limits.
        if (supportsMeshShader_) {
            VkPhysicalDeviceMeshShaderPropertiesEXT meshProps{};
            meshProps.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MESH_SHADER_PROPERTIES_EXT;
            VkPhysicalDeviceProperties2 p2{};
            p2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
            p2.pNext = &meshProps;
            vkGetPhysicalDeviceProperties2(physicalDevice_, &p2);
            f.maxMeshOutputVertices   = meshProps.maxMeshOutputVertices;
            f.maxMeshOutputPrimitives = meshProps.maxMeshOutputPrimitives;
            f.maxMeshWorkgroupSize    = meshProps.maxMeshWorkGroupInvocations;
            f.maxTaskWorkgroupSize    = meshProps.maxTaskWorkGroupInvocations;
        }

        // Limits.
        f.maxBindGroups                    = properties_.limits.maxBoundDescriptorSets;
        f.maxBindingsPerGroup              = properties_.limits.maxDescriptorSetUniformBuffers;
        f.maxPushConstantSize              = properties_.limits.maxPushConstantsSize;
        f.maxTextureDimension2D            = properties_.limits.maxImageDimension2D;
        f.maxTextureArrayLayers            = properties_.limits.maxImageArrayLayers;
        f.maxComputeWorkgroupSizeX         = properties_.limits.maxComputeWorkGroupSize[0];
        f.maxComputeWorkgroupSizeY         = properties_.limits.maxComputeWorkGroupSize[1];
        f.maxComputeWorkgroupSizeZ         = properties_.limits.maxComputeWorkGroupSize[2];
        f.maxComputeWorkgroupsPerDimension = properties_.limits.maxComputeWorkGroupCount[0];
        f.maxBufferSize                    = static_cast<u64>(properties_.limits.maxStorageBufferRange);
        f.minUniformBufferOffsetAlignment  = static_cast<u32>(properties_.limits.minUniformBufferOffsetAlignment);
        f.minStorageBufferOffsetAlignment  = static_cast<u32>(properties_.limits.minStorageBufferOffsetAlignment);
        f.timestampPeriodNs                = static_cast<u32>(properties_.limits.timestampPeriod);

        return f;
    }

    // ---- Queue family selection ----

    /// Finds the best queue family index for the given type.
    /// Prefers dedicated families for Compute and Transfer.
    i32 findQueueFamily(QueueType type) const {
        const auto count = static_cast<i32>(queueFamilies_.Size());
        switch (type) {
        case QueueType::Graphics:
            for (i32 i = 0; i < count; ++i)
                if (queueFamilies_[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) return i;
            break;
        case QueueType::Compute:
            // Prefer dedicated (no graphics).
            for (i32 i = 0; i < count; ++i) {
                auto f = queueFamilies_[i].queueFlags;
                if ((f & VK_QUEUE_COMPUTE_BIT) && !(f & VK_QUEUE_GRAPHICS_BIT)) return i;
            }
            for (i32 i = 0; i < count; ++i)
                if (queueFamilies_[i].queueFlags & VK_QUEUE_COMPUTE_BIT) return i;
            break;
        case QueueType::Transfer:
            // Prefer dedicated (no graphics or compute).
            for (i32 i = 0; i < count; ++i) {
                auto f = queueFamilies_[i].queueFlags;
                if ((f & VK_QUEUE_TRANSFER_BIT) && !(f & VK_QUEUE_GRAPHICS_BIT) && !(f & VK_QUEUE_COMPUTE_BIT))
                    return i;
            }
            for (i32 i = 0; i < count; ++i)
                if (queueFamilies_[i].queueFlags & VK_QUEUE_TRANSFER_BIT) return i;
            break;
        }
        return -1;
    }

    /// Finds a memory type index matching the filter and property requirements.
    i32 findMemoryType(u32 typeFilter, VkMemoryPropertyFlags properties) const {
        for (u32 i = 0; i < memoryProperties_.memoryTypeCount; ++i) {
            if ((typeFilter & (1 << i)) &&
                (memoryProperties_.memoryTypes[i].propertyFlags & properties) == properties)
                return static_cast<i32>(i);
        }
        return -1;
    }

    /// Maps a MemoryLocation to VkMemoryPropertyFlags.
    static VkMemoryPropertyFlags getMemoryFlags(MemoryLocation loc) {
        switch (loc) {
        case MemoryLocation::GpuOnly:  return VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
        case MemoryLocation::CpuToGpu: return VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
        case MemoryLocation::GpuToCpu: return VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT;
        case MemoryLocation::Auto:     return VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
        }
        return 0;
    }

    // ---- Internal accessors ----
    [[nodiscard]] VkPhysicalDevice                       physicalDevice()   const { return physicalDevice_; }
    [[nodiscard]] const VkPhysicalDeviceProperties&      properties()       const { return properties_; }
    [[nodiscard]] const VkPhysicalDeviceFeatures&        features10()       const { return features10_; }
    [[nodiscard]] const VkPhysicalDeviceMemoryProperties& memoryProperties() const { return memoryProperties_; }
    [[nodiscard]] const Array<VkQueueFamilyProperties>& queueFamilies() const { return queueFamilies_; }

    [[nodiscard]] bool supportsDescriptorIndexing() const { return supportsDescriptorIndexing_; }
    [[nodiscard]] bool supportsMeshShader()         const { return supportsMeshShader_; }
    [[nodiscard]] bool supportsRayTracing()         const { return supportsRayTracing_; }

private:
    void queryExtensionSupport() {
        u32 extCount = 0;
        vkEnumerateDeviceExtensionProperties(physicalDevice_, nullptr, &extCount, nullptr);
        Array<VkExtensionProperties> exts(extCount);
        vkEnumerateDeviceExtensionProperties(physicalDevice_, nullptr, &extCount, exts.Data());

        for (const auto& ext : exts) {
            const char* name = ext.extensionName;
            if (std::strcmp(name, "VK_KHR_dynamic_rendering") == 0)  supportsDynamicRendering_ = true;
            if (std::strcmp(name, "VK_KHR_timeline_semaphore") == 0) supportsTimelineSemaphore_ = true;
            if (std::strcmp(name, "VK_KHR_synchronization2") == 0)   supportsSynchronization2_ = true;
            if (std::strcmp(name, "VK_EXT_descriptor_indexing") == 0) supportsDescriptorIndexing_ = true;
            if (std::strcmp(name, "VK_EXT_mesh_shader") == 0)        supportsMeshShader_ = true;
            if (std::strcmp(name, "VK_KHR_ray_tracing_pipeline") == 0) supportsRayTracing_ = true;
        }

        // Vulkan 1.3+: these are core.
        u32 major = VK_API_VERSION_MAJOR(properties_.apiVersion);
        u32 minor = VK_API_VERSION_MINOR(properties_.apiVersion);
        if (major > 1 || (major == 1 && minor >= 3)) {
            supportsDynamicRendering_ = true;
            supportsTimelineSemaphore_ = true;
            supportsSynchronization2_ = true;
            supportsDescriptorIndexing_ = true;
        } else if (major == 1 && minor >= 2) {
            supportsTimelineSemaphore_ = true;
            supportsDescriptorIndexing_ = true;
        }
    }

    VkPhysicalDevice                   physicalDevice_ = VK_NULL_HANDLE;
    VkInstance                         instance_       = VK_NULL_HANDLE;
    VkPhysicalDeviceProperties         properties_{};
    VkPhysicalDeviceFeatures           features10_{};
    VkPhysicalDeviceMemoryProperties   memoryProperties_{};
    Array<VkQueueFamilyProperties> queueFamilies_;

    bool supportsDynamicRendering_   = false;
    bool supportsTimelineSemaphore_  = false;
    bool supportsSynchronization2_   = false;
    bool supportsDescriptorIndexing_ = false;
    bool supportsMeshShader_         = false;
    bool supportsRayTracing_         = false;
};

} // namespace raptor::rhi::vk
