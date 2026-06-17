/// Vulkan implementation of ShaderModule.
/// Ported from Sedulous.RHI.Vulkan/VulkanShaderModule.bf.

module;

#include "VkIncludes.h"

export module raptor.rhi.vk:shader_module;

import raptor.core;
import raptor.rhi;

export namespace raptor::rhi::vk {

class VkShaderModuleImpl : public ShaderModule {
public:
    Status init(VkDevice device, const ShaderModuleDesc& d) {
        VkShaderModuleCreateInfo ci{};
        ci.sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        ci.codeSize = d.code.count();
        ci.pCode    = reinterpret_cast<const u32*>(d.code.data());

        if (vkCreateShaderModule(device, &ci, nullptr, &module_) != VK_SUCCESS) return ErrorCode::Unknown;
        return ErrorCode::Ok;
    }

    void cleanup(VkDevice device) {
        if (module_ != VK_NULL_HANDLE) { vkDestroyShaderModule(device, module_, nullptr); module_ = VK_NULL_HANDLE; }
    }

    [[nodiscard]] VkShaderModule handle() const { return module_; }

private:
    VkShaderModule module_ = VK_NULL_HANDLE;
};

} // namespace raptor::rhi::vk
