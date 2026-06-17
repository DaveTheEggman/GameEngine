/// Vulkan implementation of CommandPool.
/// Ported from Sedulous.RHI.Vulkan/VulkanCommandPool.bf.

module;
#include "Core/Prelude.h"

#include "VkIncludes.h"


export module raptor.rhi.vk:command_pool;

import raptor.core;
import raptor.rhi;
import :adapter;
import :command_buffer;

using namespace raptor::core;

export namespace raptor::rhi::vk {

class VkDeviceImpl;        // forward
class VkCommandEncoderImpl; // forward

class VkCommandPoolImpl : public CommandPool {
public:
    Status init(VkDevice device, VkAdapterImpl* adapter, QueueType queueType) {
        device_ = device;

        i32 familyIndex = adapter->findQueueFamily(queueType);
        if (familyIndex < 0) return ErrorCode::Unknown;
        familyIndex_ = static_cast<u32>(familyIndex);

        VkCommandPoolCreateInfo ci{};
        ci.sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        ci.flags            = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
        ci.queueFamilyIndex = familyIndex_;

        if (vkCreateCommandPool(device, &ci, nullptr, &pool_) != VK_SUCCESS) return ErrorCode::Unknown;
        return ErrorCode::Ok;
    }

    // ---- CommandPool interface ----
    Status createEncoder(CommandEncoder*& out) override;
    void   destroyEncoder(CommandEncoder*& encoder) override;
    void   reset() override;

    void cleanup() {
        for (auto* cb : trackedBuffers_) delete cb;
        trackedBuffers_.Clear();
        freeHandles_.Clear();

        if (pool_ != VK_NULL_HANDLE) { vkDestroyCommandPool(device_, pool_, nullptr); pool_ = VK_NULL_HANDLE; }
    }

    // Called by encoder's finish() to register the command buffer.
    void trackCommandBuffer(VkCommandBufferImpl* cb) { trackedBuffers_.PushBack(cb); }

    [[nodiscard]] VkCommandPool handle() const { return pool_; }
    [[nodiscard]] VkDevice      vkDevice() const { return device_; }

    // Stored so the encoder can access it.
    VkDeviceImpl* ownerDevice = nullptr;

private:
    VkDevice                          device_      = VK_NULL_HANDLE;
    VkCommandPool                     pool_        = VK_NULL_HANDLE;
    u32                               familyIndex_ = 0;
    Array<VkCommandBuffer>      freeHandles_;
    Array<VkCommandBufferImpl*> trackedBuffers_;
};

} // namespace raptor::rhi::vk
