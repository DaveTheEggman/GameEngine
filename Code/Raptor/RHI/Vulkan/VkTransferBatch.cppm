/// Vulkan implementation of TransferBatch.
/// Ported from Sedulous.RHI.Vulkan/VulkanTransferBatch.bf.

module;
#include "Core/Prelude.h"

#include "VkIncludes.h"

#include <cstring>

export module raptor.rhi.vk:transfer_batch;

import raptor.core;
import raptor.rhi;
import :conversions;
import :buffer;
import :texture;
import :fence;

using namespace raptor::core;

export namespace raptor::rhi::vk {

class VkTransferBatchImpl : public TransferBatch {
public:
    VkTransferBatchImpl(VkDevice device, VkQueue queue, u32 queueFamilyIndex, VkPhysicalDevice physDevice)
        : device_(device), physDevice_(physDevice), queue_(queue), queueFamilyIndex_(queueFamilyIndex) {}

    void writeBuffer(Buffer* dst, u64 dstOffset, Span<const u8> data) override {
        u64 needed = stagingOffset_ + data.Size();
        if (ensureStagingBuffer(needed) != ErrorCode::Ok) return;
        void* mapped = stagingMapped_;
        if (!mapped) return;
        std::memcpy(static_cast<u8*>(mapped) + stagingOffset_, data.Data(), data.Size());
        bufferCopies_.PushBack({ dst, dstOffset, stagingOffset_, data.Size() });
        stagingOffset_ = (stagingOffset_ + data.Size() + 15) & ~static_cast<u64>(15);
    }

    void writeTexture(Texture* dst, Span<const u8> data,
                      const TextureDataLayout& layout, Extent3D extent,
                      u32 mipLevel, u32 arrayLayer) override {
        u64 needed = stagingOffset_ + data.Size();
        if (ensureStagingBuffer(needed) != ErrorCode::Ok) return;
        void* mapped = stagingMapped_;
        if (!mapped) return;
        std::memcpy(static_cast<u8*>(mapped) + stagingOffset_, data.Data(), data.Size());
        textureCopies_.PushBack({ dst, stagingOffset_, mipLevel, arrayLayer, extent, layout });
        stagingOffset_ = (stagingOffset_ + data.Size() + 15) & ~static_cast<u64>(15);
    }

    Status submit() override {
        if (bufferCopies_.IsEmpty() && textureCopies_.IsEmpty()) return ErrorCode::Ok;
        VkCommandBuffer cmdBuf = recordCommands();
        if (cmdBuf == VK_NULL_HANDLE) return ErrorCode::Unknown;

        VkSubmitInfo si{}; si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        si.commandBufferCount = 1; si.pCommandBuffers = &cmdBuf;
        if (vkQueueSubmit(queue_, 1, &si, VK_NULL_HANDLE) != VK_SUCCESS) return ErrorCode::Unknown;
        vkQueueWaitIdle(queue_);
        cleanupCmdPool();
        return ErrorCode::Ok;
    }

    Status submitAsync(Fence* fence, u64 signalValue) override {
        if (bufferCopies_.IsEmpty() && textureCopies_.IsEmpty()) return ErrorCode::Ok;
        VkCommandBuffer cmdBuf = recordCommands();
        if (cmdBuf == VK_NULL_HANDLE) return ErrorCode::Unknown;

        auto* vkFence = static_cast<VkFenceImpl*>(fence);
        if (!vkFence) return ErrorCode::Unknown;

        VkSemaphore sem = vkFence->handle();
        VkTimelineSemaphoreSubmitInfo tsi{}; tsi.sType = VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO;
        tsi.signalSemaphoreValueCount = 1; tsi.pSignalSemaphoreValues = &signalValue;

        VkSubmitInfo si{}; si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO; si.pNext = &tsi;
        si.commandBufferCount = 1; si.pCommandBuffers = &cmdBuf;
        si.signalSemaphoreCount = 1; si.pSignalSemaphores = &sem;

        vkQueueSubmit(queue_, 1, &si, VK_NULL_HANDLE);
        asyncFence_ = vkFence; asyncValue_ = signalValue;
        return ErrorCode::Ok;
    }

    void reset() override {
        bufferCopies_.Clear(); textureCopies_.Clear();
        stagingOffset_ = 0;
        cleanupCmdPool();
    }

    void destroy() override {
        if (asyncFence_) { asyncFence_->wait(asyncValue_, ~0ull); asyncFence_ = nullptr; }
        if (cmdPool_ != VK_NULL_HANDLE) { vkDestroyCommandPool(device_, cmdPool_, nullptr); cmdPool_ = VK_NULL_HANDLE; }
        if (stagingMapped_) { vkUnmapMemory(device_, stagingMem_); stagingMapped_ = nullptr; }
        if (stagingMem_ != VK_NULL_HANDLE) { vkFreeMemory(device_, stagingMem_, nullptr); stagingMem_ = VK_NULL_HANDLE; }
        if (stagingBuf_ != VK_NULL_HANDLE) { vkDestroyBuffer(device_, stagingBuf_, nullptr); stagingBuf_ = VK_NULL_HANDLE; }
    }

private:
    struct BufCopy { Buffer* dst; u64 dstOffset; u64 stagingOffset; u64 size; };
    struct TexCopy { Texture* dst; u64 stagingOffset; u32 mipLevel; u32 arrayLayer; Extent3D extent; TextureDataLayout layout; };

    Status ensureStagingBuffer(u64 required) {
        if (stagingBuf_ != VK_NULL_HANDLE && stagingSize_ >= required) return ErrorCode::Ok;
        u64 newSize = Max(required, Max(stagingSize_ * 2, static_cast<u64>(4 * 1024 * 1024)));

        // Create staging buffer directly.
        VkBufferCreateInfo ci{}; ci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        ci.size = newSize; ci.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT; ci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        VkBuffer newBuf = VK_NULL_HANDLE;
        if (vkCreateBuffer(device_, &ci, nullptr, &newBuf) != VK_SUCCESS) return ErrorCode::Unknown;

        VkMemoryRequirements memReqs{}; vkGetBufferMemoryRequirements(device_, newBuf, &memReqs);

        // Find host-visible + host-coherent memory type.
        VkPhysicalDeviceMemoryProperties memProps{};
        vkGetPhysicalDeviceMemoryProperties(physDevice_, &memProps);
        i32 memType = -1;
        for (u32 i = 0; i < memProps.memoryTypeCount; ++i) {
            if ((memReqs.memoryTypeBits & (1 << i)) &&
                (memProps.memoryTypes[i].propertyFlags & (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT))
                    == (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT))
            { memType = static_cast<i32>(i); break; }
        }
        if (memType < 0) { vkDestroyBuffer(device_, newBuf, nullptr); return ErrorCode::Unknown; }

        VkMemoryAllocateInfo ai{}; ai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        ai.allocationSize = memReqs.size; ai.memoryTypeIndex = static_cast<u32>(memType);
        VkDeviceMemory newMem = VK_NULL_HANDLE;
        if (vkAllocateMemory(device_, &ai, nullptr, &newMem) != VK_SUCCESS) {
            vkDestroyBuffer(device_, newBuf, nullptr); return ErrorCode::Unknown;
        }
        vkBindBufferMemory(device_, newBuf, newMem, 0);

        void* newMapped = nullptr;
        vkMapMemory(device_, newMem, 0, newSize, 0, &newMapped);

        // Copy existing data.
        if (stagingMapped_ && stagingOffset_ > 0 && newMapped)
            std::memcpy(newMapped, stagingMapped_, static_cast<usize>(stagingOffset_));

        // Free old.
        if (stagingMapped_) vkUnmapMemory(device_, stagingMem_);
        if (stagingMem_ != VK_NULL_HANDLE) vkFreeMemory(device_, stagingMem_, nullptr);
        if (stagingBuf_ != VK_NULL_HANDLE) vkDestroyBuffer(device_, stagingBuf_, nullptr);

        stagingBuf_ = newBuf; stagingMem_ = newMem; stagingMapped_ = newMapped; stagingSize_ = newSize;
        return ErrorCode::Ok;
    }

    VkCommandBuffer recordCommands() {
        if (cmdPool_ == VK_NULL_HANDLE) {
            VkCommandPoolCreateInfo ci{}; ci.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
            ci.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT; ci.queueFamilyIndex = queueFamilyIndex_;
            if (vkCreateCommandPool(device_, &ci, nullptr, &cmdPool_) != VK_SUCCESS) return VK_NULL_HANDLE;
        }
        VkCommandBufferAllocateInfo ai{}; ai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        ai.commandPool = cmdPool_; ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY; ai.commandBufferCount = 1;
        VkCommandBuffer cb = VK_NULL_HANDLE;
        if (vkAllocateCommandBuffers(device_, &ai, &cb) != VK_SUCCESS) return VK_NULL_HANDLE;

        VkCommandBufferBeginInfo bi{}; bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(cb, &bi);

        for (const auto& c : bufferCopies_) {
            auto* dst = static_cast<VkBufferImpl*>(c.dst);
            if (!dst) continue;
            VkBufferCopy r{}; r.srcOffset = c.stagingOffset; r.dstOffset = c.dstOffset; r.size = c.size;
            vkCmdCopyBuffer(cb, stagingBuf_, dst->handle(), 1, &r);
        }

        for (const auto& c : textureCopies_) {
            auto* dst = static_cast<VkTextureImpl*>(c.dst);
            if (!dst) continue;
            auto aspect = getAspectMask(dst->desc.format);

            VkImageMemoryBarrier pre{}; pre.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            pre.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            pre.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED; pre.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            pre.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED; pre.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            pre.image = dst->handle();
            pre.subresourceRange = { aspect, c.mipLevel, 1, c.arrayLayer, 1 };
            vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                                 0, 0, nullptr, 0, nullptr, 1, &pre);

            VkBufferImageCopy r{}; r.bufferOffset = c.stagingOffset;
            r.imageSubresource = { aspect, c.mipLevel, c.arrayLayer, 1 };
            r.imageExtent = { c.extent.width, c.extent.height, c.extent.depth };
            vkCmdCopyBufferToImage(cb, stagingBuf_, dst->handle(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &r);

            VkImageMemoryBarrier post = pre;
            post.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT; post.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            post.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL; post.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                                 0, 0, nullptr, 0, nullptr, 1, &post);
        }

        vkEndCommandBuffer(cb);
        return cb;
    }

    void cleanupCmdPool() {
        if (cmdPool_ != VK_NULL_HANDLE) vkResetCommandPool(device_, cmdPool_, 0);
    }

    VkDevice         device_     = VK_NULL_HANDLE;
    VkPhysicalDevice physDevice_ = VK_NULL_HANDLE;
    VkQueue          queue_      = VK_NULL_HANDLE;
    u32              queueFamilyIndex_ = 0;
    VkCommandPool    cmdPool_    = VK_NULL_HANDLE;
    VkBuffer         stagingBuf_ = VK_NULL_HANDLE;
    VkDeviceMemory   stagingMem_ = VK_NULL_HANDLE;
    void*            stagingMapped_ = nullptr;
    u64              stagingOffset_ = 0;
    u64              stagingSize_   = 0;
    Array<BufCopy> bufferCopies_;
    Array<TexCopy> textureCopies_;
    VkFenceImpl*   asyncFence_ = nullptr;
    u64            asyncValue_  = 0;
};

} // namespace raptor::rhi::vk
