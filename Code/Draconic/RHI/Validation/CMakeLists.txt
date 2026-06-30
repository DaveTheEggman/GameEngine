# Draconic::RHIValidation — `draconic.rhi.validation` (a wrapping layer that checks
# RHI API usage and forwards to an inner backend).

add_library(DraconicRHIValidation STATIC)
add_library(Draconic::RHIValidation ALIAS DraconicRHIValidation)

target_link_libraries(DraconicRHIValidation PUBLIC Draconic::RHI Draconic::Core Draconic::Policy)

target_sources(DraconicRHIValidation
    PUBLIC
        FILE_SET rhi_validation_modules TYPE CXX_MODULES FILES
            ValidatedAdapter.cppm
            ValidatedFence.cppm
            ValidatedTransferBatch.cppm
            ValidatedRenderBundleEncoder.cppm
            ValidatedCommandEncoder.cppm
            ValidatedComputePassEncoder.cppm
            ValidatedRenderPassEncoder.cppm
            ValidatedCommandPool.cppm
            ValidatedQueue.cppm
            ValidatedSwapChain.cppm
            ValidatedDevice.cppm
            ValidatedBackend.cppm
            ValidatedWiring.cppm
            ValidationModule.cppm)

if(DRACONIC_BUILD_TESTS)
    add_subdirectory(Tests)
endif()
