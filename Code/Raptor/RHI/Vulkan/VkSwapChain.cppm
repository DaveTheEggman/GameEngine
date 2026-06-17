/// Vulkan implementation of SwapChain.
/// Ported from Sedulous.RHI.Vulkan/VulkanSwapChain.bf.

module;
#include "Core/Prelude.h"

#include "VkIncludes.h"


export module raptor.rhi.vk:swap_chain;

import raptor.core;
import raptor.rhi;
import :conversions;
import :adapter;
import :surface;
import :texture;
import :texture_view;

using namespace raptor::core;

export namespace raptor::rhi::vk {

// Minimal reverse format mapping for swap chain format negotiation.
inline TextureFormat fromVkFormat(VkFormat f) {
    switch (f) {
    case VK_FORMAT_R8G8B8A8_UNORM: return TextureFormat::RGBA8Unorm;
    case VK_FORMAT_R8G8B8A8_SRGB:  return TextureFormat::RGBA8UnormSrgb;
    case VK_FORMAT_B8G8R8A8_UNORM: return TextureFormat::BGRA8Unorm;
    case VK_FORMAT_B8G8R8A8_SRGB:  return TextureFormat::BGRA8UnormSrgb;
    case VK_FORMAT_R16G16B16A16_SFLOAT: return TextureFormat::RGBA16Float;
    case VK_FORMAT_A2B10G10R10_UNORM_PACK32: return TextureFormat::RGB10A2Unorm;
    default: return TextureFormat::Undefined;
    }
}

class VkDeviceImpl; // forward

class VkSwapChainImpl : public SwapChain {
public:
    Status init(VkDevice device, VkPhysicalDevice physDevice, VkSurfaceKHR surface,
                const SwapChainDesc& desc, VkDeviceImpl* owner) {
        device_     = device;
        physDevice_ = physDevice;
        surface_    = surface;
        owner_      = owner;
        presentMode_= desc.presentMode;
        return CreateSwapChain(desc.width, desc.height, desc.format, desc.bufferCount, VK_NULL_HANDLE);
    }

    // ---- SwapChain interface ----
    TextureFormat Format()            const override { return format_; }
    u32           Width()             const override { return width_; }
    u32           Height()            const override { return height_; }
    u32           BufferCount()       const override { return bufferCount_; }
    u32           CurrentImageIndex() const override { return currentImageIndex_; }

    Status AcquireNextImage() override;
    Texture*     CurrentTexture()     override { return currentImageIndex_ < textures_.Size() ? textures_[currentImageIndex_] : nullptr; }
    TextureView* CurrentTextureView() override { return currentImageIndex_ < views_.Size()    ? views_[currentImageIndex_]    : nullptr; }
    Status Present(Queue* queue) override;
    Status Resize(u32 w, u32 h) override;

    void cleanup();

    // ---- Internal ----
    [[nodiscard]] VkSwapchainKHR handle() const { return swapchain_; }

    // Semaphore accessors for queue submit integration.
    VkSemaphore currentAcquireSemaphore() const { return acquireSems_[frameIndex_]; }
    VkSemaphore currentPresentSemaphore() const { return presentSems_[currentImageIndex_]; }

private:
    Status CreateSwapChain(u32 w, u32 h, TextureFormat reqFormat, u32 reqCount, VkSwapchainKHR old);
    Status retrieveImages(VkFormat format);
    void   createSyncObjects();
    void   cleanupImages();
    void   destroySyncObjects();

    VkSurfaceFormatKHR chooseSurfaceFormat(TextureFormat requested);
    VkPresentModeKHR   choosePresentMode(PresentMode requested);

    VkDevice         device_     = VK_NULL_HANDLE;
    VkPhysicalDevice physDevice_ = VK_NULL_HANDLE;
    VkSurfaceKHR     surface_    = VK_NULL_HANDLE;
    VkSwapchainKHR   swapchain_  = VK_NULL_HANDLE;
    VkDeviceImpl*    owner_      = nullptr;

    TextureFormat format_      = TextureFormat::Undefined;
    PresentMode   presentMode_ = PresentMode::Fifo;
    u32 width_  = 0, height_ = 0, bufferCount_ = 0;
    u32 currentImageIndex_ = 0;
    u32 frameIndex_        = 0;

    Array<VkTextureImpl*>     textures_;
    Array<VkTextureViewImpl*> views_;
    Array<VkSemaphore>        acquireSems_;
    Array<VkSemaphore>        presentSems_;
};

// ---- Implementation (inline in module) ----

inline Status VkSwapChainImpl::CreateSwapChain(u32 w, u32 h, TextureFormat reqFormat, u32 reqCount, VkSwapchainKHR old) {
    VkSurfaceCapabilitiesKHR caps{};
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physDevice_, surface_, &caps);

    if (caps.currentExtent.width != ~0u) { width_ = caps.currentExtent.width; height_ = caps.currentExtent.height; }
    else { width_ = Clamp(w, caps.minImageExtent.width, caps.maxImageExtent.width);
           height_ = Clamp(h, caps.minImageExtent.height, caps.maxImageExtent.height); }
    if (width_ == 0 || height_ == 0) return ErrorCode::Unknown;

    bufferCount_ = Max(reqCount, caps.minImageCount);
    if (caps.maxImageCount > 0) bufferCount_ = Min(bufferCount_, caps.maxImageCount);

    auto surfFmt = chooseSurfaceFormat(reqFormat);
    format_ = fromVkFormat(surfFmt.format);
    if (format_ == TextureFormat::Undefined) format_ = reqFormat;

    auto presentMode = choosePresentMode(presentMode_);

    VkImageUsageFlags usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    if (caps.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_DST_BIT) usage |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    if (caps.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_SRC_BIT) usage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;

    VkCompositeAlphaFlagBitsKHR compositeAlpha = (caps.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR)
        ? VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR : VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR;

    VkSwapchainCreateInfoKHR ci{};
    ci.sType            = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    ci.surface          = surface_;
    ci.minImageCount    = bufferCount_;
    ci.imageFormat      = surfFmt.format;
    ci.imageColorSpace  = surfFmt.colorSpace;
    ci.imageExtent      = { width_, height_ };
    ci.imageArrayLayers = 1;
    ci.imageUsage       = usage;
    ci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    ci.preTransform     = caps.currentTransform;
    ci.compositeAlpha   = compositeAlpha;
    ci.presentMode      = presentMode;
    ci.clipped          = VK_TRUE;
    ci.oldSwapchain     = old;

    if (vkCreateSwapchainKHR(device_, &ci, nullptr, &swapchain_) != VK_SUCCESS) return ErrorCode::Unknown;
    if (old != VK_NULL_HANDLE) vkDestroySwapchainKHR(device_, old, nullptr);

    if (retrieveImages(surfFmt.format) != ErrorCode::Ok) return ErrorCode::Unknown;
    createSyncObjects();
    frameIndex_ = 0;
    return ErrorCode::Ok;
}

inline VkSurfaceFormatKHR VkSwapChainImpl::chooseSurfaceFormat(TextureFormat requested) {
    u32 count = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(physDevice_, surface_, &count, nullptr);
    Array<VkSurfaceFormatKHR> fmts(count);
    vkGetPhysicalDeviceSurfaceFormatsKHR(physDevice_, surface_, &count, fmts.Data());

    VkFormat desired = toVkFormat(requested);
    for (auto& f : fmts) if (f.format == desired) return f;
    for (auto& f : fmts) if (f.format == VK_FORMAT_B8G8R8A8_SRGB && f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) return f;
    for (auto& f : fmts) if (f.format == VK_FORMAT_B8G8R8A8_UNORM) return f;
    return fmts[0];
}

inline VkPresentModeKHR VkSwapChainImpl::choosePresentMode(PresentMode requested) {
    u32 count = 0;
    vkGetPhysicalDeviceSurfacePresentModesKHR(physDevice_, surface_, &count, nullptr);
    Array<VkPresentModeKHR> modes(count);
    vkGetPhysicalDeviceSurfacePresentModesKHR(physDevice_, surface_, &count, modes.Data());
    VkPresentModeKHR desired = toVkPresentMode(requested);
    for (auto m : modes) if (m == desired) return m;
    return VK_PRESENT_MODE_FIFO_KHR;
}

inline Status VkSwapChainImpl::retrieveImages(VkFormat format) {
    u32 imgCount = 0;
    vkGetSwapchainImagesKHR(device_, swapchain_, &imgCount, nullptr);
    Array<VkImage> images(imgCount);
    vkGetSwapchainImagesKHR(device_, swapchain_, &imgCount, images.Data());
    bufferCount_ = imgCount;

    TextureFormat texFmt = fromVkFormat(format);
    if (texFmt == TextureFormat::Undefined) texFmt = format_;

    for (u32 i = 0; i < imgCount; ++i) {
        TextureDesc td{}; td.dimension = TextureDimension::Texture2D; td.format = texFmt;
        td.width = width_; td.height = height_; td.arrayLayerCount = 1; td.mipLevelCount = 1;
        td.sampleCount = 1; td.usage = TextureUsage::RenderTarget;

        auto* tex = new VkTextureImpl();
        tex->initFromExisting(images[i], td);
        textures_.PushBack(tex);

        TextureViewDesc vd{}; vd.format = texFmt; vd.dimension = TextureViewDimension::Texture2D;
        vd.mipLevelCount = 1; vd.arrayLayerCount = 1;
        auto* view = new VkTextureViewImpl();
        if (view->init(device_, tex, vd) != ErrorCode::Ok) { delete view; return ErrorCode::Unknown; }
        views_.PushBack(view);
    }
    return ErrorCode::Ok;
}

inline void VkSwapChainImpl::createSyncObjects() {
    VkSemaphoreCreateInfo ci{}; ci.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    for (u32 i = 0; i < bufferCount_; ++i) {
        VkSemaphore a = VK_NULL_HANDLE, p = VK_NULL_HANDLE;
        vkCreateSemaphore(device_, &ci, nullptr, &a);
        vkCreateSemaphore(device_, &ci, nullptr, &p);
        acquireSems_.PushBack(a);
        presentSems_.PushBack(p);
    }
}

inline void VkSwapChainImpl::cleanupImages() {
    for (auto* v : views_)    { v->cleanup(device_); delete v; } views_.Clear();
    for (auto* t : textures_) { t->cleanup(device_); delete t; } textures_.Clear();
}

inline void VkSwapChainImpl::destroySyncObjects() {
    for (auto s : acquireSems_) { vkDestroySemaphore(device_, s, nullptr); }
    acquireSems_.Clear();
    for (auto s : presentSems_) { vkDestroySemaphore(device_, s, nullptr); }
    presentSems_.Clear();
}

inline Status VkSwapChainImpl::Resize(u32 w, u32 h) {
    vkDeviceWaitIdle(device_);
    cleanupImages();
    destroySyncObjects();
    return CreateSwapChain(w, h, format_, bufferCount_, swapchain_);
}

inline void VkSwapChainImpl::cleanup() {
    vkDeviceWaitIdle(device_);
    cleanupImages();
    destroySyncObjects();
    if (swapchain_ != VK_NULL_HANDLE) { vkDestroySwapchainKHR(device_, swapchain_, nullptr); swapchain_ = VK_NULL_HANDLE; }
}

} // namespace raptor::rhi::vk
