/// Vulkan implementation of Queue.
/// Ported from Sedulous.RHI.Vulkan/VulkanQueue.bf.

module;

#include "VkIncludes.h"

#include <vector>

export module raptor.rhi.vk:queue;

import raptor.core;
import raptor.rhi;
import :command_buffer;
import :fence;
import :transfer_batch;

export namespace raptor::rhi::vk {

class VkDeviceImpl; // forward

class VkQueueImpl : public Queue {
public:
    VkQueueImpl(VkQueue queue, QueueType type, u32 familyIndex, f32 tsPeriod, VkDeviceImpl* device, VkDevice vkDevice, VkPhysicalDevice physDevice)
        : queue_(queue), familyIndex_(familyIndex), tsPeriod_(tsPeriod), device_(device), vkDevice_(vkDevice), physDevice_(physDevice)
    { queueType = type; }

    // ---- Queue interface ----

    void submit(Span<CommandBuffer* const> cmdBufs) override {
        if (cmdBufs.count() == 0) return;
        std::vector<VkCommandBuffer> bufs(cmdBufs.count());
        for (usize i = 0; i < cmdBufs.count(); ++i)
            bufs[i] = static_cast<VkCommandBufferImpl*>(cmdBufs[i])->handle();

        VkSubmitInfo si{}; si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        si.commandBufferCount = static_cast<u32>(bufs.size());
        si.pCommandBuffers    = bufs.data();
        vkQueueSubmit(queue_, 1, &si, VK_NULL_HANDLE);
    }

    void submit(Span<CommandBuffer* const> cmdBufs, Fence* signalFence, u64 signalValue) override;

    void submit(Span<CommandBuffer* const> cmdBufs,
                Span<Fence* const> waitFences, Span<const u64> waitValues,
                Fence* signalFence, u64 signalValue) override;


    void waitIdle() override { vkQueueWaitIdle(queue_); }

    Status createTransferBatch(TransferBatch*& out) override {
        out = new VkTransferBatchImpl(vkDevice_, queue_, familyIndex_, physDevice_);
        return ErrorCode::Ok;
    }
    void destroyTransferBatch(TransferBatch*& batch) override {
        if (batch) { static_cast<VkTransferBatchImpl*>(batch)->destroy(); delete batch; batch = nullptr; }
    }

    f32 timestampPeriod() const override { return tsPeriod_; }

    // ---- Internal ----
    [[nodiscard]] VkQueue handle()      const { return queue_; }
    [[nodiscard]] u32     familyIndex() const { return familyIndex_; }

private:
    VkQueue       queue_       = VK_NULL_HANDLE;
    u32           familyIndex_ = 0;
    f32           tsPeriod_    = 0.0f;
    VkDeviceImpl*    device_      = nullptr;
    VkDevice         vkDevice_    = VK_NULL_HANDLE;
    VkPhysicalDevice physDevice_  = VK_NULL_HANDLE;
};

} // namespace raptor::rhi::vk
