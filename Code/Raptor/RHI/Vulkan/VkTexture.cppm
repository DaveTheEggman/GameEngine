/// Vulkan implementation of Texture.
/// Ported from Sedulous.RHI.Vulkan/VulkanTexture.bf.

module;

#include "VkIncludes.h"

#include <algorithm>
#include <vector>

export module raptor.rhi.vk:texture;

import raptor.core;
import raptor.rhi;
import :adapter;
import :conversions;

export namespace raptor::rhi::vk {

class VkTextureImpl : public Texture {
public:
    /// Initialize from a TextureDesc (creates VkImage + allocates memory).
    Status init(VkDevice device, VkAdapterImpl* adapter, const TextureDesc& d) {
        desc = d;

        VkImageCreateInfo ci{};
        ci.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        ci.imageType     = toVkImageType(d.dimension);
        ci.format        = toVkFormat(d.format);
        ci.extent        = { d.width, d.height, d.depth };
        ci.mipLevels     = d.mipLevelCount;
        ci.arrayLayers   = d.arrayLayerCount;
        ci.samples       = toVkSampleCount(d.sampleCount);
        ci.tiling        = VK_IMAGE_TILING_OPTIMAL;
        ci.usage         = toVkImageUsage(d.usage);
        ci.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
        ci.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

        if (d.arrayLayerCount >= 6 && d.dimension == TextureDimension::Texture2D)
            ci.flags |= VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;

        if (vkCreateImage(device, &ci, nullptr, &image_) != VK_SUCCESS) return ErrorCode::Unknown;

        VkMemoryRequirements memReqs{};
        vkGetImageMemoryRequirements(device, image_, &memReqs);

        i32 memType = adapter->findMemoryType(
            static_cast<u32>(memReqs.memoryTypeBits), VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        if (memType < 0) {
            vkDestroyImage(device, image_, nullptr); image_ = VK_NULL_HANDLE;
            return ErrorCode::Unknown;
        }

        VkMemoryAllocateInfo ai{};
        ai.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        ai.allocationSize  = memReqs.size;
        ai.memoryTypeIndex = static_cast<u32>(memType);

        if (vkAllocateMemory(device, &ai, nullptr, &memory_) != VK_SUCCESS) {
            vkDestroyImage(device, image_, nullptr); image_ = VK_NULL_HANDLE;
            return ErrorCode::Unknown;
        }

        vkBindImageMemory(device, image_, memory_, 0);
        return ErrorCode::Ok;
    }

    /// Initialize from an existing VkImage (e.g. swap chain). Does not own the image.
    void initFromExisting(VkImage image, const TextureDesc& d) {
        image_     = image;
        desc       = d;
        ownsImage_ = false;
    }

    void cleanup(VkDevice device) {
        if (memory_ != VK_NULL_HANDLE) { vkFreeMemory(device, memory_, nullptr); memory_ = VK_NULL_HANDLE; }
        if (ownsImage_ && image_ != VK_NULL_HANDLE) vkDestroyImage(device, image_, nullptr);
        image_ = VK_NULL_HANDLE;
        subresourceLayouts_.clear();
    }

    // ---- Internal ----
    [[nodiscard]] VkImage  handle()   const { return image_; }
    [[nodiscard]] VkFormat vkFormat() const { return toVkFormat(desc.format); }

    /// Whole-resource layout (uniform fast path).
    VkImageLayout currentLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    /// Get layout for a specific subresource.
    VkImageLayout getSubresourceLayout(u32 mip, u32 layer) const {
        if (subresourceLayouts_.empty()) return currentLayout;
        u32 idx = mip + layer * desc.mipLevelCount;
        if (idx >= static_cast<u32>(subresourceLayouts_.size())) return currentLayout;
        return subresourceLayouts_[idx];
    }

    /// Update layout for a subresource range. Promotes to per-subresource
    /// tracking when needed, collapses back to uniform when all match.
    void setSubresourceLayout(u32 baseMip, u32 mipCount, u32 baseLayer, u32 layerCount,
                              VkImageLayout layout) {
        u32 totalMips   = desc.mipLevelCount;
        u32 totalLayers = std::max(desc.arrayLayerCount, 1u);
        u32 mipEnd   = (mipCount   == ~0u) ? totalMips   : std::min(baseMip   + mipCount,   totalMips);
        u32 layerEnd = (layerCount == ~0u) ? totalLayers : std::min(baseLayer + layerCount, totalLayers);

        // All subresources? Collapse to uniform.
        if (baseMip == 0 && mipEnd >= totalMips && baseLayer == 0 && layerEnd >= totalLayers) {
            currentLayout = layout;
            subresourceLayouts_.clear();
            return;
        }

        // Promote to per-subresource.
        if (subresourceLayouts_.empty()) {
            if (layout == currentLayout) return;
            subresourceLayouts_.resize(totalMips * totalLayers, currentLayout);
        }

        for (u32 l = baseLayer; l < layerEnd; ++l)
            for (u32 m = baseMip; m < mipEnd; ++m)
                subresourceLayouts_[m + l * totalMips] = layout;

        // Try to collapse back to uniform.
        VkImageLayout first = subresourceLayouts_[0];
        for (usize i = 1; i < subresourceLayouts_.size(); ++i) {
            if (subresourceLayouts_[i] != first) return;
        }
        currentLayout = first;
        subresourceLayouts_.clear();
    }

private:
    VkImage        image_     = VK_NULL_HANDLE;
    VkDeviceMemory memory_    = VK_NULL_HANDLE;
    bool           ownsImage_ = true;
    std::vector<VkImageLayout> subresourceLayouts_;
};

} // namespace raptor::rhi::vk
