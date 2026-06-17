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
        m_device = device;

        i32 familyIndex = adapter->findQueueFamily(queueType);
        if (familyIndex < 0) return ErrorCode::Unknown;
        m_familyIndex = static_cast<u32>(familyIndex);

        VkCommandPoolCreateInfo ci{};
        ci.sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        ci.flags            = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
        ci.queueFamilyIndex = m_familyIndex;

        if (vkCreateCommandPool(device, &ci, nullptr, &m_pool) != VK_SUCCESS) return ErrorCode::Unknown;
        return ErrorCode::Ok;
    }

    // ---- CommandPool interface ----
    Status CreateEncoder(CommandEncoder*& out) override;
    void   DestroyEncoder(CommandEncoder*& encoder) override;
    void   Reset() override;

    void cleanup() {
        for (auto* cb : m_trackedBuffers) delete cb;
        m_trackedBuffers.Clear();
        m_freeHandles.Clear();

        if (m_pool != VK_NULL_HANDLE) { vkDestroyCommandPool(m_device, m_pool, nullptr); m_pool = VK_NULL_HANDLE; }
    }

    // Called by encoder's finish() to register the command buffer.
    void trackCommandBuffer(VkCommandBufferImpl* cb) { m_trackedBuffers.PushBack(cb); }

    [[nodiscard]] VkCommandPool handle() const { return m_pool; }
    [[nodiscard]] VkDevice      vkDevice() const { return m_device; }

    // Stored so the encoder can access it.
    VkDeviceImpl* ownerDevice = nullptr;

private:
    VkDevice                          m_device      = VK_NULL_HANDLE;
    VkCommandPool                     m_pool        = VK_NULL_HANDLE;
    u32                               m_familyIndex = 0;
    Array<VkCommandBuffer>      m_freeHandles;
    Array<VkCommandBufferImpl*> m_trackedBuffers;
};

} // namespace raptor::rhi::vk
