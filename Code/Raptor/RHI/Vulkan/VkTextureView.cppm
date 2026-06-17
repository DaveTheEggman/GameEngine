/// Vulkan implementation of TextureView.
/// Ported from Sedulous.RHI.Vulkan/VulkanTextureView.bf.

module;

#include "VkIncludes.h"

export module raptor.rhi.vk:texture_view;

import raptor.core;
import raptor.rhi;
import :conversions;
import :texture;

export namespace raptor::rhi::vk {

class VkTextureViewImpl : public TextureView {
public:
    Status init(VkDevice device, VkTextureImpl* tex, const TextureViewDesc& d) {
        desc    = d;
        texture = tex;
        width_  = tex->desc.width;
        height_ = tex->desc.height;

        TextureFormat fmt = (d.format == TextureFormat::Undefined) ? tex->desc.format : d.format;
        format_ = fmt;

        VkImageViewCreateInfo ci{};
        ci.sType    = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        ci.image    = tex->handle();
        ci.viewType = toVkImageViewType(d.dimension);
        ci.format   = toVkFormat(fmt);
        ci.components = { VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY,
                          VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY };

        u32 mipCount   = d.mipLevelCount   > 0 ? d.mipLevelCount   : tex->desc.mipLevelCount   - d.baseMipLevel;
        u32 layerCount = d.arrayLayerCount  > 0 ? d.arrayLayerCount : tex->desc.arrayLayerCount - d.baseArrayLayer;

        VkImageAspectFlags aspect;
        switch (d.aspect) {
        case TextureAspect::DepthOnly:   aspect = VK_IMAGE_ASPECT_DEPTH_BIT; break;
        case TextureAspect::StencilOnly: aspect = VK_IMAGE_ASPECT_STENCIL_BIT; break;
        default:                         aspect = getAspectMask(fmt); break;
        }

        ci.subresourceRange = { aspect, d.baseMipLevel, mipCount, d.baseArrayLayer, layerCount };

        if (vkCreateImageView(device, &ci, nullptr, &imageView_) != VK_SUCCESS) return ErrorCode::Unknown;
        return ErrorCode::Ok;
    }

    void cleanup(VkDevice device) {
        if (imageView_ != VK_NULL_HANDLE) {
            vkDestroyImageView(device, imageView_, nullptr);
            imageView_ = VK_NULL_HANDLE;
        }
    }

    [[nodiscard]] VkImageView   handle() const { return imageView_; }
    [[nodiscard]] TextureFormat format() const { return format_; }
    [[nodiscard]] u32           width()  const { return width_; }
    [[nodiscard]] u32           height() const { return height_; }

private:
    VkImageView   imageView_ = VK_NULL_HANDLE;
    TextureFormat format_    = TextureFormat::Undefined;
    u32           width_     = 0;
    u32           height_    = 0;
};

} // namespace raptor::rhi::vk
