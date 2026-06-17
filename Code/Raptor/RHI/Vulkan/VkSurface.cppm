/// Vulkan implementation of Surface.
/// Wraps VkSurfaceKHR + parent VkInstance for cleanup.

module;

#include "VkIncludes.h"

export module raptor.rhi.vk:surface;

import raptor.rhi;

export namespace raptor::rhi::vk {

class VkSurfaceImpl : public Surface {
public:
    VkSurfaceImpl(VkSurfaceKHR surface, VkInstance instance)
        : surface_(surface), instance_(instance) {}

    [[nodiscard]] VkSurfaceKHR handle() const { return surface_; }

    void destroy() {
        if (surface_ != VK_NULL_HANDLE) {
            vkDestroySurfaceKHR(instance_, surface_, nullptr);
            surface_ = VK_NULL_HANDLE;
        }
    }

private:
    VkSurfaceKHR surface_  = VK_NULL_HANDLE;
    VkInstance   instance_ = VK_NULL_HANDLE;
};

} // namespace raptor::rhi::vk
