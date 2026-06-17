/// Vulkan implementation of CommandBuffer.
/// Ported from Sedulous.RHI.Vulkan/VulkanCommandBuffer.bf.

module;
#include "Core/Prelude.h"

#include "VkIncludes.h"

export module raptor.rhi.vk:command_buffer;

import raptor.core;
import raptor.rhi;

using namespace raptor::core;

export namespace raptor::rhi::vk {

class VkCommandBufferImpl : public CommandBuffer {
public:
    explicit VkCommandBufferImpl(VkCommandBuffer cmdBuf) : cmdBuf_(cmdBuf) {}

    [[nodiscard]] VkCommandBuffer handle() const { return cmdBuf_; }

private:
    VkCommandBuffer cmdBuf_ = VK_NULL_HANDLE;
};

} // namespace raptor::rhi::vk
