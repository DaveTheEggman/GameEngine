/// Vulkan implementation of ComputePipeline.
/// Ported from Sedulous.RHI.Vulkan/VulkanComputePipeline.bf.

module;

#include "VkIncludes.h"

#include <string>

export module raptor.rhi.vk:compute_pipeline;

import raptor.core;
import raptor.rhi;
import :shader_module;
import :pipeline_layout;
import :pipeline_cache;

export namespace raptor::rhi::vk {

class VkComputePipelineImpl : public ComputePipeline {
public:
    Status init(VkDevice device, const ComputePipelineDesc& desc) {
        auto* vkLayout = static_cast<VkPipelineLayoutImpl*>(desc.layout);
        if (!vkLayout) return ErrorCode::Unknown;
        layout  = desc.layout;
        layout_ = vkLayout;

        auto* vkMod = static_cast<VkShaderModuleImpl*>(desc.compute.module);
        if (!vkMod) return ErrorCode::Unknown;

        std::string entry(desc.compute.entryPoint.data(), desc.compute.entryPoint.length());

        VkPipelineShaderStageCreateInfo stage{};
        stage.sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stage.stage  = VK_SHADER_STAGE_COMPUTE_BIT;
        stage.module = vkMod->handle();
        stage.pName  = entry.c_str();

        VkComputePipelineCreateInfo ci{};
        ci.sType  = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
        ci.stage  = stage;
        ci.layout = vkLayout->handle();

        VkPipelineCache cacheHandle = VK_NULL_HANDLE;
        if (desc.cache) cacheHandle = static_cast<VkPipelineCacheImpl*>(desc.cache)->handle();

        if (vkCreateComputePipelines(device, cacheHandle, 1, &ci, nullptr, &pipeline_) != VK_SUCCESS)
            return ErrorCode::Unknown;
        return ErrorCode::Ok;
    }

    void cleanup(VkDevice device) {
        if (pipeline_ != VK_NULL_HANDLE) { vkDestroyPipeline(device, pipeline_, nullptr); pipeline_ = VK_NULL_HANDLE; }
    }

    [[nodiscard]] VkPipeline           handle()   const { return pipeline_; }
    [[nodiscard]] VkPipelineLayoutImpl* vkLayout() const { return layout_; }

private:
    VkPipeline            pipeline_ = VK_NULL_HANDLE;
    VkPipelineLayoutImpl* layout_   = nullptr;
};

} // namespace raptor::rhi::vk
