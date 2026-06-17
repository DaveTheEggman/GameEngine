/// Vulkan implementation of PipelineLayout.
/// Ported from Sedulous.RHI.Vulkan/VulkanPipelineLayout.bf.

module;

#include "VkIncludes.h"

#include <vector>

export module raptor.rhi.vk:pipeline_layout;

import raptor.core;
import raptor.rhi;
import :conversions;
import :bind_group_layout;

export namespace raptor::rhi::vk {

class VkPipelineLayoutImpl : public PipelineLayout {
public:
    Status init(VkDevice device, const PipelineLayoutDesc& desc) {
        std::vector<VkDescriptorSetLayout> setLayouts(desc.bindGroupLayouts.count());
        for (usize i = 0; i < desc.bindGroupLayouts.count(); ++i) {
            auto* vkl = static_cast<VkBindGroupLayoutImpl*>(desc.bindGroupLayouts[i]);
            if (!vkl) return ErrorCode::Unknown;
            setLayouts[i] = vkl->handle();
        }

        std::vector<VkPushConstantRange> pushRanges(desc.pushConstantRanges.count());
        for (usize i = 0; i < desc.pushConstantRanges.count(); ++i) {
            pushRanges[i] = {};
            pushRanges[i].stageFlags = toVkShaderStageFlags(desc.pushConstantRanges[i].stages);
            pushRanges[i].offset     = desc.pushConstantRanges[i].offset;
            pushRanges[i].size       = desc.pushConstantRanges[i].size;
        }

        VkPipelineLayoutCreateInfo ci{};
        ci.sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        ci.setLayoutCount         = static_cast<u32>(setLayouts.size());
        ci.pSetLayouts            = setLayouts.data();
        ci.pushConstantRangeCount = static_cast<u32>(pushRanges.size());
        ci.pPushConstantRanges    = pushRanges.data();

        if (vkCreatePipelineLayout(device, &ci, nullptr, &layout_) != VK_SUCCESS) return ErrorCode::Unknown;
        return ErrorCode::Ok;
    }

    void cleanup(VkDevice device) {
        if (layout_ != VK_NULL_HANDLE) { vkDestroyPipelineLayout(device, layout_, nullptr); layout_ = VK_NULL_HANDLE; }
    }

    [[nodiscard]] VkPipelineLayout handle() const { return layout_; }

private:
    VkPipelineLayout layout_ = VK_NULL_HANDLE;
};

} // namespace raptor::rhi::vk
