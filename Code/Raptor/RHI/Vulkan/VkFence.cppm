/// Vulkan implementation of Fence (timeline semaphore).
/// Ported from Sedulous.RHI.Vulkan/VulkanFence.bf.

module;

#include "VkIncludes.h"

export module raptor.rhi.vk:fence;

import raptor.core;
import raptor.rhi;

export namespace raptor::rhi::vk {

class VkFenceImpl : public Fence {
public:
    Status init(VkDevice device, u64 initialValue) {
        device_ = device;

        VkSemaphoreTypeCreateInfo typeInfo{};
        typeInfo.sType         = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO;
        typeInfo.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
        typeInfo.initialValue  = initialValue;

        VkSemaphoreCreateInfo ci{};
        ci.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
        ci.pNext = &typeInfo;

        if (vkCreateSemaphore(device, &ci, nullptr, &semaphore_) != VK_SUCCESS) return ErrorCode::Unknown;
        return ErrorCode::Ok;
    }

    void cleanup(VkDevice device) {
        if (semaphore_ != VK_NULL_HANDLE) {
            vkDestroySemaphore(device, semaphore_, nullptr);
            semaphore_ = VK_NULL_HANDLE;
        }
    }

    // ---- Fence interface ----
    u64 completedValue() override {
        u64 value = 0;
        vkGetSemaphoreCounterValue(device_, semaphore_, &value);
        return value;
    }

    bool wait(u64 value, u64 timeoutNs) override {
        VkSemaphoreWaitInfo wi{};
        wi.sType          = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO;
        wi.semaphoreCount = 1;
        wi.pSemaphores    = &semaphore_;
        wi.pValues        = &value;
        return vkWaitSemaphores(device_, &wi, timeoutNs) == VK_SUCCESS;
    }

    [[nodiscard]] VkSemaphore handle() const { return semaphore_; }

private:
    VkSemaphore semaphore_ = VK_NULL_HANDLE;
    VkDevice    device_    = VK_NULL_HANDLE;
};

} // namespace raptor::rhi::vk
