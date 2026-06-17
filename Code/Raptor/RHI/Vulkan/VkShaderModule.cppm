/// Vulkan implementation of ShaderModule.
/// Ported from Sedulous.RHI.Vulkan/VulkanShaderModule.bf.

module;
#include "Core/Prelude.h"

#include "VkIncludes.h"

export module raptor.rhi.vk:shader_module;

import raptor.core;
import raptor.rhi;

using namespace raptor::core;

export namespace raptor::rhi::vk {

class VkShaderModuleImpl : public ShaderModule {
public:
    Status init(VkDevice device, const ShaderModuleDesc& d) {
        VkShaderModuleCreateInfo ci{};
        ci.sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        ci.codeSize = d.code.Size();
        ci.pCode    = reinterpret_cast<const u32*>(d.code.Data());

        if (vkCreateShaderModule(device, &ci, nullptr, &m_module) != VK_SUCCESS) return ErrorCode::Unknown;
        return ErrorCode::Ok;
    }

    void cleanup(VkDevice device) {
        if (m_module != VK_NULL_HANDLE) { vkDestroyShaderModule(device, m_module, nullptr); m_module = VK_NULL_HANDLE; }
    }

    [[nodiscard]] VkShaderModule handle() const { return m_module; }

private:
    VkShaderModule m_module = VK_NULL_HANDLE;
};

} // namespace raptor::rhi::vk
