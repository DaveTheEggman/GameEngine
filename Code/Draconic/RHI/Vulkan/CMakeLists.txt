# Draconic::RHIVulkan — `draconic.rhi.vk` (the Vulkan RHI backend). Links the Vulkan
# loader; built only when the Vulkan SDK/headers are available.

add_library(DraconicRHIVulkan STATIC)
add_library(Draconic::RHIVulkan ALIAS DraconicRHIVulkan)

target_link_libraries(DraconicRHIVulkan
    PUBLIC Draconic::RHI Draconic::Core Draconic::Policy
    PRIVATE Vulkan::Vulkan)

target_sources(DraconicRHIVulkan
    PUBLIC
        FILE_SET rhi_vk_modules TYPE CXX_MODULES FILES
            VkConversions.cppm
            VkSurface.cppm
            VkAdapter.cppm
            VkBackend.cppm
            VkBuffer.cppm
            VkTexture.cppm
            VkTextureView.cppm
            VkSampler.cppm
            VkShaderModule.cppm
            VkFence.cppm
            VkQuerySet.cppm
            VkBarrierHelper.cppm
            VkCommandBuffer.cppm
            VkDescriptorPoolManager.cppm
            VkBindingShifts.cppm
            VkBindGroupLayout.cppm
            VkPipelineLayout.cppm
            VkPipelineCache.cppm
            VkRenderPipeline.cppm
            VkComputePipeline.cppm
            VkBindGroup.cppm
            VkSwapChain.cppm
            VkQueue.cppm
            VkCommandPool.cppm
            VkRenderBundleEncoder.cppm
            VkRenderPassEncoder.cppm
            VkComputePassEncoder.cppm
            VkMeshPipeline.cppm
            VkAccelStruct.cppm
            VkRayTracingPipeline.cppm
            VkTransferBatch.cppm
            VkCommandEncoder.cppm
            VkDevice.cppm
            VkModule.cppm)
