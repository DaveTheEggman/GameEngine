/// Vulkan implementation of AccelStruct (acceleration structure).
/// Ported from Sedulous.RHI.Vulkan/VulkanAccelStruct.bf.

module;
#include "Core/Prelude.h"

#include "VkIncludes.h"

export module raptor.rhi.vk:accel_struct;

import raptor.core;
import raptor.rhi;
import :adapter;

using namespace raptor::core;

export namespace raptor::rhi::vk {

class VkAccelStructImpl : public AccelStruct {
public:
    Status init(VkDevice device, VkAdapterImpl* adapter, const AccelStructDesc& desc, u64 size) {
        type_ = desc.type;
        device_ = device;

        // Create buffer for the acceleration structure.
        VkBufferCreateInfo bufCi{};
        bufCi.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufCi.size  = size;
        bufCi.usage = VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
        if (vkCreateBuffer(device, &bufCi, nullptr, &buffer_) != VK_SUCCESS) return ErrorCode::Unknown;

        VkMemoryRequirements memReqs{};
        vkGetBufferMemoryRequirements(device, buffer_, &memReqs);

        i32 memType = adapter->findMemoryType(static_cast<u32>(memReqs.memoryTypeBits), VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        if (memType < 0) { vkDestroyBuffer(device, buffer_, nullptr); buffer_ = VK_NULL_HANDLE; return ErrorCode::Unknown; }

        VkMemoryAllocateFlagsInfo allocFlags{};
        allocFlags.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO;
        allocFlags.flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT;

        VkMemoryAllocateInfo ai{};
        ai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        ai.pNext = &allocFlags;
        ai.allocationSize  = memReqs.size;
        ai.memoryTypeIndex = static_cast<u32>(memType);
        if (vkAllocateMemory(device, &ai, nullptr, &memory_) != VK_SUCCESS) {
            vkDestroyBuffer(device, buffer_, nullptr); buffer_ = VK_NULL_HANDLE; return ErrorCode::Unknown;
        }
        vkBindBufferMemory(device, buffer_, memory_, 0);

        // Create acceleration structure.
        VkAccelerationStructureCreateInfoKHR asCi{};
        asCi.sType  = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR;
        asCi.buffer = buffer_;
        asCi.size   = size;
        asCi.type   = desc.type == AccelStructType::TopLevel
            ? VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR
            : VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;

        auto pfnCreate = reinterpret_cast<PFN_vkCreateAccelerationStructureKHR>(
            vkGetDeviceProcAddr(device, "vkCreateAccelerationStructureKHR"));
        if (!pfnCreate || pfnCreate(device, &asCi, nullptr, &accel_) != VK_SUCCESS) return ErrorCode::Unknown;

        // Get device address.
        VkAccelerationStructureDeviceAddressInfoKHR addrInfo{};
        addrInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR;
        addrInfo.accelerationStructure = accel_;
        auto pfnAddr = reinterpret_cast<PFN_vkGetAccelerationStructureDeviceAddressKHR>(
            vkGetDeviceProcAddr(device, "vkGetAccelerationStructureDeviceAddressKHR"));
        if (pfnAddr) deviceAddress_ = pfnAddr(device, &addrInfo);

        return ErrorCode::Ok;
    }

    void cleanup(VkDevice device) {
        if (accel_ != VK_NULL_HANDLE) {
            auto pfn = reinterpret_cast<PFN_vkDestroyAccelerationStructureKHR>(
                vkGetDeviceProcAddr(device, "vkDestroyAccelerationStructureKHR"));
            if (pfn) pfn(device, accel_, nullptr);
            accel_ = VK_NULL_HANDLE;
        }
        if (memory_ != VK_NULL_HANDLE) { vkFreeMemory(device, memory_, nullptr); memory_ = VK_NULL_HANDLE; }
        if (buffer_ != VK_NULL_HANDLE) { vkDestroyBuffer(device, buffer_, nullptr); buffer_ = VK_NULL_HANDLE; }
    }

    AccelStructType Type()          const override { return type_; }
    u64             DeviceAddress() const override { return deviceAddress_; }

    [[nodiscard]] VkAccelerationStructureKHR handle() const { return accel_; }

private:
    VkAccelerationStructureKHR accel_  = VK_NULL_HANDLE;
    VkBuffer                   buffer_ = VK_NULL_HANDLE;
    VkDeviceMemory             memory_ = VK_NULL_HANDLE;
    VkDevice                   device_ = VK_NULL_HANDLE;
    AccelStructType            type_   = AccelStructType::BottomLevel;
    u64                        deviceAddress_ = 0;
};

} // namespace raptor::rhi::vk
