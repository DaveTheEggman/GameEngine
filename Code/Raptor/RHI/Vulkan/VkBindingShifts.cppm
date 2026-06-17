/// HLSL register binding shift configuration.
/// Ported from Sedulous.RHI.Vulkan/VulkanDevice.bf (VulkanBindingShifts).

export module raptor.rhi.vk:binding_shifts;

import raptor.core;
import raptor.rhi;

using namespace raptor::core;

export namespace raptor::rhi::vk {

/// Maps HLSL register spaces to Vulkan descriptor bindings.
/// DXC's -fvk-*-shift flags use these values when compiling HLSL to SPIR-V.
struct BindingShifts {
    u32 cbvShift     = 0;     ///< Constant buffer (b) register shift.
    u32 srvShift     = 0;     ///< Shader resource view (t) register shift.
    u32 uavShift     = 0;     ///< Unordered access view (u) register shift.
    u32 samplerShift = 0;     ///< Sampler (s) register shift.

    /// Standard layout: CBV=0, SRV=1000, UAV=2000, Sampler=3000.
    static constexpr BindingShifts standard() {
        return { 0, 1000, 2000, 3000 };
    }

    /// Applies the appropriate shift for a binding type.
    u32 apply(BindingType type, u32 binding) const {
        switch (type) {
        case BindingType::UniformBuffer:
            return binding + cbvShift;
        case BindingType::SampledTexture:
        case BindingType::BindlessTextures:
        case BindingType::AccelerationStructure:
            return binding + srvShift;
        case BindingType::StorageTextureReadOnly:
        case BindingType::StorageTextureReadWrite:
        case BindingType::BindlessStorageTextures:
        case BindingType::StorageBufferReadOnly:
        case BindingType::StorageBufferReadWrite:
        case BindingType::BindlessStorageBuffers:
            return binding + uavShift;
        case BindingType::Sampler:
        case BindingType::ComparisonSampler:
        case BindingType::BindlessSamplers:
            return binding + samplerShift;
        }
        return binding;
    }
};

} // namespace raptor::rhi::vk
