/// Vulkan implementation of Buffer.
/// Ported from Sedulous.RHI.Vulkan/VulkanBuffer.bf.

module;

#include "VkIncludes.h"

export module raptor.rhi.vk:buffer;

import raptor.core;
import raptor.rhi;
import :adapter;
import :conversions;

export namespace raptor::rhi::vk {

class VkDeviceImpl; // forward

class VkBufferImpl : public Buffer {
public:
    Status init(VkDevice device, VkAdapterImpl* adapter, const BufferDesc& d) {
        desc = d;

        VkBufferCreateInfo ci{};
        ci.sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        ci.size        = d.size;
        ci.usage       = toVkBufferUsage(d.usage);
        ci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        if (vkCreateBuffer(device, &ci, nullptr, &buffer_) != VK_SUCCESS) return ErrorCode::Unknown;

        VkMemoryRequirements memReqs{};
        vkGetBufferMemoryRequirements(device, buffer_, &memReqs);

        auto memFlags = VkAdapterImpl::getMemoryFlags(d.memory);
        i32 memType = adapter->findMemoryType(static_cast<u32>(memReqs.memoryTypeBits), memFlags);

        if (memType < 0 && d.memory == MemoryLocation::Auto)
            memType = adapter->findMemoryType(static_cast<u32>(memReqs.memoryTypeBits),
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

        if (memType < 0) {
            vkDestroyBuffer(device, buffer_, nullptr);
            buffer_ = VK_NULL_HANDLE;
            return ErrorCode::Unknown;
        }

        bool needsDeviceAddress = hasFlag(d.usage, BufferUsage::AccelStructInput)
                               || hasFlag(d.usage, BufferUsage::ShaderBindingTable)
                               || hasFlag(d.usage, BufferUsage::AccelStructScratch);

        VkMemoryAllocateFlagsInfo allocFlags{};
        allocFlags.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO;
        if (needsDeviceAddress)
            allocFlags.flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT;

        VkMemoryAllocateInfo allocInfo{};
        allocInfo.sType          = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        if (needsDeviceAddress) allocInfo.pNext = &allocFlags;
        allocInfo.allocationSize  = memReqs.size;
        allocInfo.memoryTypeIndex = static_cast<u32>(memType);

        if (vkAllocateMemory(device, &allocInfo, nullptr, &memory_) != VK_SUCCESS) {
            vkDestroyBuffer(device, buffer_, nullptr);
            buffer_ = VK_NULL_HANDLE;
            return ErrorCode::Unknown;
        }

        vkBindBufferMemory(device, buffer_, memory_, 0);

        // Persistently map host-visible buffers.
        if (d.memory == MemoryLocation::CpuToGpu || d.memory == MemoryLocation::GpuToCpu)
            vkMapMemory(device, memory_, 0, d.size, 0, &mappedPtr_);

        return ErrorCode::Ok;
    }

    void cleanup(VkDevice device) {
        if (mappedPtr_) { vkUnmapMemory(device, memory_); mappedPtr_ = nullptr; }
        if (memory_ != VK_NULL_HANDLE) { vkFreeMemory(device, memory_, nullptr); memory_ = VK_NULL_HANDLE; }
        if (buffer_ != VK_NULL_HANDLE) { vkDestroyBuffer(device, buffer_, nullptr); buffer_ = VK_NULL_HANDLE; }
    }

    // ---- Buffer interface ----
    void* map()   override { return mappedPtr_; }
    void  unmap() override { /* persistently mapped */ }

    // ---- Internal ----
    [[nodiscard]] VkBuffer       handle() const { return buffer_; }
    [[nodiscard]] VkDeviceMemory memory() const { return memory_; }

private:
    VkBuffer       buffer_    = VK_NULL_HANDLE;
    VkDeviceMemory memory_    = VK_NULL_HANDLE;
    void*          mappedPtr_ = nullptr;
};

} // namespace raptor::rhi::vk
