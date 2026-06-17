/// Manages VkDescriptorPool allocation with auto-grow.
/// Ported from Sedulous.RHI.Vulkan/VulkanDescriptorPoolManager.bf.

module;
#include "Core/Prelude.h"

#include "VkIncludes.h"


export module raptor.rhi.vk:descriptor_pool_manager;

import raptor.core;

using namespace raptor::core;

export namespace raptor::rhi::vk {

class VkDescriptorPoolManager {
public:
    VkDescriptorPoolManager(VkDevice device, u32 maxSetsPerPool = 256, bool accelStructEnabled = false)
        : device_(device), maxSetsPerPool_(maxSetsPerPool), accelStructEnabled_(accelStructEnabled) {}

    Status allocate(VkDescriptorSetLayout layout, VkDescriptorPool& outPool,
                    bool updateAfterBind = false, u32 variableCount = 0) {
        outPool = VK_NULL_HANDLE;

        VkDescriptorSetVariableDescriptorCountAllocateInfo varCountInfo{};
        varCountInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_VARIABLE_DESCRIPTOR_COUNT_ALLOCATE_INFO;
        if (variableCount > 0) {
            varCountInfo.descriptorSetCount = 1;
            varCountInfo.pDescriptorCounts  = &variableCount;
        }

        // Try existing pools.
        for (auto pool : pools_) {
            VkDescriptorSet set = VK_NULL_HANDLE;
            VkDescriptorSetAllocateInfo ai{};
            ai.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
            ai.descriptorPool     = pool;
            ai.descriptorSetCount = 1;
            ai.pSetLayouts        = &layout;
            if (variableCount > 0) ai.pNext = &varCountInfo;

            if (vkAllocateDescriptorSets(device_, &ai, &set) == VK_SUCCESS) {
                outPool = pool;
                lastAllocatedSet_ = set;
                return ErrorCode::Ok;
            }
        }

        // Create new pool.
        if (createPool(updateAfterBind) != ErrorCode::Ok) return ErrorCode::Unknown;

        auto pool = pools_.Back();
        VkDescriptorSet set = VK_NULL_HANDLE;
        VkDescriptorSetAllocateInfo ai{};
        ai.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        ai.descriptorPool     = pool;
        ai.descriptorSetCount = 1;
        ai.pSetLayouts        = &layout;
        if (variableCount > 0) ai.pNext = &varCountInfo;

        if (vkAllocateDescriptorSets(device_, &ai, &set) != VK_SUCCESS) return ErrorCode::Unknown;

        outPool = pool;
        lastAllocatedSet_ = set;
        return ErrorCode::Ok;
    }

    [[nodiscard]] VkDescriptorSet lastAllocatedSet() const { return lastAllocatedSet_; }

    void free(VkDescriptorPool pool, VkDescriptorSet set) {
        vkFreeDescriptorSets(device_, pool, 1, &set);
    }

    void Destroy() {
        for (auto pool : pools_) vkDestroyDescriptorPool(device_, pool, nullptr);
        pools_.Clear();
    }

private:
    Status createPool(bool updateAfterBind) {
        u32 mult = updateAfterBind ? 64 : 1;
        VkDescriptorPoolSize sizes[] = {
            { VK_DESCRIPTOR_TYPE_SAMPLER,                    maxSetsPerPool_ * mult },
            { VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,              maxSetsPerPool_ * 4 * mult },
            { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,              maxSetsPerPool_ * mult },
            { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,             maxSetsPerPool_ * 2 },
            { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,             maxSetsPerPool_ * 2 * mult },
            { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,     maxSetsPerPool_ },
            { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC,     maxSetsPerPool_ },
            { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,     maxSetsPerPool_ * 4 * mult },
            { VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT,           maxSetsPerPool_ },
            { VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER,       maxSetsPerPool_ },
            { VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER,       maxSetsPerPool_ },
            { VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, maxSetsPerPool_ },
        };
        u32 sizeCount = accelStructEnabled_ ? 12 : 11;

        VkDescriptorPoolCreateInfo ci{};
        ci.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        ci.flags         = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
        if (updateAfterBind) ci.flags |= VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT;
        ci.maxSets       = maxSetsPerPool_;
        ci.poolSizeCount = sizeCount;
        ci.pPoolSizes    = sizes;

        VkDescriptorPool pool = VK_NULL_HANDLE;
        if (vkCreateDescriptorPool(device_, &ci, nullptr, &pool) != VK_SUCCESS) return ErrorCode::Unknown;
        pools_.PushBack(pool);
        return ErrorCode::Ok;
    }

    VkDevice                      device_;
    Array<VkDescriptorPool> pools_;
    u32                           maxSetsPerPool_;
    bool                          accelStructEnabled_;
    VkDescriptorSet               lastAllocatedSet_ = VK_NULL_HANDLE;
};

} // namespace raptor::rhi::vk
