/// Vulkan implementation of Queue.
/// Ported from Sedulous.RHI.Vulkan/VulkanQueue.bf.

module;
#include "Core/Prelude.h"

#include "VkIncludes.h"


export module raptor.rhi.vk:queue;

import raptor.core;
import raptor.rhi;
import :command_buffer;
import :fence;
import :transfer_batch;

using namespace raptor::core;

export namespace raptor::rhi::vk {

class VkDeviceImpl; // forward

class VkQueueImpl : public Queue {
public:
    VkQueueImpl(VkQueue queue, QueueType type, u32 familyIndex, f32 tsPeriod, VkDeviceImpl* device, VkDevice vkDevice, VkPhysicalDevice physDevice)
        : queue_(queue), familyIndex_(familyIndex), tsPeriod_(tsPeriod), device_(device), vkDevice_(vkDevice), physDevice_(physDevice)
    { queueType = type; }

    // ---- Queue interface ----

    void Submit(Span<CommandBuffer* const> cmdBufs) override {
        if (cmdBufs.Size() == 0) return;
        Array<VkCommandBuffer> bufs(cmdBufs.Size());
        for (usize i = 0; i < cmdBufs.Size(); ++i)
            bufs[i] = static_cast<VkCommandBufferImpl*>(cmdBufs[i])->handle();

        VkSubmitInfo si{}; si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        si.commandBufferCount = static_cast<u32>(bufs.Size());
        si.pCommandBuffers    = bufs.Data();
        vkQueueSubmit(queue_, 1, &si, VK_NULL_HANDLE);
    }

    void Submit(Span<CommandBuffer* const> cmdBufs, Fence* signalFence, u64 signalValue) override;

    void Submit(Span<CommandBuffer* const> cmdBufs,
                Span<Fence* const> waitFences, Span<const u64> waitValues,
                Fence* signalFence, u64 signalValue) override;


    void WaitIdle() override { vkQueueWaitIdle(queue_); }

    Status CreateTransferBatch(TransferBatch*& out) override {
        out = new VkTransferBatchImpl(vkDevice_, queue_, familyIndex_, physDevice_);
        return ErrorCode::Ok;
    }
    void DestroyTransferBatch(TransferBatch*& batch) override {
        if (batch) { static_cast<VkTransferBatchImpl*>(batch)->Destroy(); delete batch; batch = nullptr; }
    }

    f32 TimestampPeriod() const override { return tsPeriod_; }

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
