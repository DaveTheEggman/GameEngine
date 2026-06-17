/// Vulkan implementation of PipelineCache.
/// Ported from Sedulous.RHI.Vulkan/VulkanPipelineCache.bf.

module;

#include "VkIncludes.h"

export module raptor.rhi.vk:pipeline_cache;

import raptor.core;
import raptor.rhi;

export namespace raptor::rhi::vk {

class VkPipelineCacheImpl : public PipelineCache {
public:
    Status init(VkDevice device, const PipelineCacheDesc& desc) {
        device_ = device;

        VkPipelineCacheCreateInfo ci{};
        ci.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
        if (desc.initialData.count() > 0) {
            ci.initialDataSize = desc.initialData.count();
            ci.pInitialData    = desc.initialData.data();
        }

        if (vkCreatePipelineCache(device, &ci, nullptr, &cache_) != VK_SUCCESS) return ErrorCode::Unknown;
        return ErrorCode::Ok;
    }

    void cleanup(VkDevice device) {
        if (cache_ != VK_NULL_HANDLE) { vkDestroyPipelineCache(device, cache_, nullptr); cache_ = VK_NULL_HANDLE; }
    }

    u32 getDataSize() override {
        usize size = 0;
        vkGetPipelineCacheData(device_, cache_, &size, nullptr);
        return static_cast<u32>(size);
    }

    Status getData(Span<u8> outData) override {
        usize size = outData.count();
        if (vkGetPipelineCacheData(device_, cache_, &size, outData.data()) != VK_SUCCESS) return ErrorCode::Unknown;
        return ErrorCode::Ok;
    }

    [[nodiscard]] VkPipelineCache handle() const { return cache_; }

private:
    VkPipelineCache cache_  = VK_NULL_HANDLE;
    VkDevice        device_ = VK_NULL_HANDLE;
};

} // namespace raptor::rhi::vk
