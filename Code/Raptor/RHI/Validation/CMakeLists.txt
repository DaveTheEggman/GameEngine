# Raptor::RHIValidation — `raptor.rhi.validation` (a wrapping layer that checks
# RHI API usage and forwards to an inner backend).

add_library(RaptorRHIValidation STATIC)
add_library(Raptor::RHIValidation ALIAS RaptorRHIValidation)

target_link_libraries(RaptorRHIValidation PUBLIC Raptor::RHI Raptor::Core Raptor::Policy)

target_sources(RaptorRHIValidation
    PUBLIC
        FILE_SET rhi_validation_modules TYPE CXX_MODULES FILES
            ValidatedAdapter.cppm
            ValidatedFence.cppm
            ValidatedTransferBatch.cppm
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

if(RAPTOR_BUILD_TESTS)
    add_subdirectory(Tests)
endif()
