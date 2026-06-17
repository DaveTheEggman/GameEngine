/// Vulkan implementation of BindGroupLayout.
/// Ported from Sedulous.RHI.Vulkan/VulkanBindGroupLayout.bf.

module;
#include "Core/Prelude.h"

#include "VkIncludes.h"


export module raptor.rhi.vk:bind_group_layout;

import raptor.core;
import raptor.rhi;
import :conversions;
import :binding_shifts;

using namespace raptor::core;

export namespace raptor::rhi::vk {

inline VkDescriptorType toVkDescriptorType(const BindGroupLayoutEntry& e) {
    switch (e.type) {
    case BindingType::UniformBuffer:
        return e.hasDynamicOffset ? VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC : VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    case BindingType::StorageBufferReadOnly:
    case BindingType::StorageBufferReadWrite:
        return e.hasDynamicOffset ? VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC : VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    case BindingType::SampledTexture:          return VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
    case BindingType::StorageTextureReadOnly:
    case BindingType::StorageTextureReadWrite: return VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    case BindingType::Sampler:
    case BindingType::ComparisonSampler:       return VK_DESCRIPTOR_TYPE_SAMPLER;
    case BindingType::BindlessTextures:        return VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
    case BindingType::BindlessSamplers:         return VK_DESCRIPTOR_TYPE_SAMPLER;
    case BindingType::BindlessStorageBuffers:   return VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    case BindingType::BindlessStorageTextures:  return VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    case BindingType::AccelerationStructure:   return VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
    }
    return VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
}

class VkBindGroupLayoutImpl : public BindGroupLayout {
public:
    Status init(VkDevice device, const BindGroupLayoutDesc& desc, const BindingShifts& shifts = {}) {
        entries_.Clear();
        for (usize i = 0; i < desc.entries.Size(); ++i) { entries_.PushBack(desc.entries[i]); }

        Array<VkDescriptorSetLayoutBinding> bindings(desc.entries.Size());
        Array<VkDescriptorBindingFlags>     flags(desc.entries.Size());

        for (usize i = 0; i < desc.entries.Size(); ++i) {
            const auto& e = desc.entries[i];
            auto& b = bindings[i];
            b = {};
            b.binding         = shifts.apply(e.type, e.binding);
            b.descriptorType  = toVkDescriptorType(e);
            b.descriptorCount = e.count;
            b.stageFlags      = toVkShaderStageFlags(e.visibility);

            flags[i] = 0;
            if (e.count == ~0u) {
                b.descriptorCount = 1024 * 16;
                hasBindless_      = true;
                bindlessCount_    = b.descriptorCount;
                flags[i] = VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT
                         | VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT
                         | VK_DESCRIPTOR_BINDING_VARIABLE_DESCRIPTOR_COUNT_BIT;
            }
        }

        VkDescriptorSetLayoutBindingFlagsCreateInfo flagsInfo{};
        flagsInfo.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO;
        flagsInfo.bindingCount  = static_cast<u32>(desc.entries.Size());
        flagsInfo.pBindingFlags = flags.Data();

        VkDescriptorSetLayoutCreateInfo ci{};
        ci.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        ci.bindingCount = static_cast<u32>(desc.entries.Size());
        ci.pBindings    = bindings.Data();
        if (hasBindless_) {
            ci.flags |= VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT;
            ci.pNext  = &flagsInfo;
        }

        if (vkCreateDescriptorSetLayout(device, &ci, nullptr, &layout_) != VK_SUCCESS)
            return ErrorCode::Unknown;
        return ErrorCode::Ok;
    }

    void cleanup(VkDevice device) {
        if (layout_ != VK_NULL_HANDLE) { vkDestroyDescriptorSetLayout(device, layout_, nullptr); layout_ = VK_NULL_HANDLE; }
    }

    Span<const BindGroupLayoutEntry> entries() const override {
        return Span<const BindGroupLayoutEntry>(entries_.Data(), entries_.Size());
    }

    [[nodiscard]] VkDescriptorSetLayout handle() const { return layout_; }
    [[nodiscard]] bool hasBindless()   const { return hasBindless_; }
    [[nodiscard]] u32  bindlessCount() const { return bindlessCount_; }

private:
    VkDescriptorSetLayout                layout_ = VK_NULL_HANDLE;
    Array<BindGroupLayoutEntry>    entries_;
    bool                                 hasBindless_  = false;
    u32                                  bindlessCount_ = 0;
};

} // namespace raptor::rhi::vk
