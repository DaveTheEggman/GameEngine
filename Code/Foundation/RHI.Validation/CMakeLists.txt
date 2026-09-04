# Foundation::RHI.Validation - `foundation.rhi.validation` (a wrapping layer that checks
# RHI API usage and forwards to an inner backend).

util_add_engine_library(RHI.Validation ALIAS Foundation::RHI.Validation)
target_link_libraries(RHI.Validation PUBLIC Foundation::RHI Foundation::Core Foundation::Policy)

target_sources(RHI.Validation
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

