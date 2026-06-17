/// Vulkan implementation of ComputePassEncoder.
/// Ported from Sedulous.RHI.Vulkan/VulkanComputePassEncoder.bf.

module;

#include "VkIncludes.h"

export module raptor.rhi.vk:compute_pass_encoder;

import raptor.core;
import raptor.rhi;
import :conversions;
import :buffer;
import :bind_group;
import :compute_pipeline;
import :pipeline_layout;
import :query_set;

export namespace raptor::rhi::vk {

class VkComputePassEncoderImpl : public ComputePassEncoder {
public:
    VkComputePassEncoderImpl(VkCommandBuffer cmdBuf) : cmdBuf_(cmdBuf) {}

    void setPipeline(ComputePipeline* pipeline) override {
        current_ = static_cast<VkComputePipelineImpl*>(pipeline);
        if (current_) vkCmdBindPipeline(cmdBuf_, VK_PIPELINE_BIND_POINT_COMPUTE, current_->handle());
    }

    void setBindGroup(u32 index, BindGroup* group, Span<const u32> dynOffsets) override {
        auto* bg = static_cast<VkBindGroupImpl*>(group);
        if (!bg || !current_ || !current_->vkLayout()) return;
        VkDescriptorSet set = bg->handle();
        vkCmdBindDescriptorSets(cmdBuf_, VK_PIPELINE_BIND_POINT_COMPUTE,
            current_->vkLayout()->handle(), index, 1, &set,
            static_cast<u32>(dynOffsets.count()), dynOffsets.data());
    }

    void setPushConstants(ShaderStage stages, u32 offset, u32 size, const void* data) override {
        if (!current_ || !current_->vkLayout()) return;
        vkCmdPushConstants(cmdBuf_, current_->vkLayout()->handle(),
            toVkShaderStageFlags(stages), offset, size, data);
    }

    void dispatch(u32 x, u32 y, u32 z) override { vkCmdDispatch(cmdBuf_, x, y, z); }

    void dispatchIndirect(Buffer* buffer, u64 offset) override {
        auto* vkBuf = static_cast<VkBufferImpl*>(buffer);
        if (vkBuf) vkCmdDispatchIndirect(cmdBuf_, vkBuf->handle(), offset);
    }

    void computeBarrier() override {
        VkMemoryBarrier2 mb{};
        mb.sType         = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2;
        mb.srcStageMask  = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
        mb.srcAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT;
        mb.dstStageMask  = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
        mb.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_SHADER_WRITE_BIT;
        VkDependencyInfo di{};
        di.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
        di.memoryBarrierCount = 1; di.pMemoryBarriers = &mb;
        vkCmdPipelineBarrier2(cmdBuf_, &di);
    }

    void writeTimestamp(QuerySet* qs, u32 index) override {
        auto* q = static_cast<VkQuerySetImpl*>(qs);
        if (q) vkCmdWriteTimestamp(cmdBuf_, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, q->handle(), index);
    }

    void end() override { current_ = nullptr; }

private:
    VkCommandBuffer        cmdBuf_  = VK_NULL_HANDLE;
    VkComputePipelineImpl* current_ = nullptr;
};

} // namespace raptor::rhi::vk
