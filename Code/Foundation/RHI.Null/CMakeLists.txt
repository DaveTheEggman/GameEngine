# Foundation::RHI.Null - `foundation.rhi.null` (headless no-op RHI backend, for tests/tools).

util_add_engine_library(RHI.Null ALIAS Foundation::RHI.Null)
target_link_libraries(RHI.Null PUBLIC Foundation::RHI Foundation::Core Foundation::Policy)

target_sources(RHI.Null
    PUBLIC
        FILE_SET rhi_null_modules TYPE CXX_MODULES FILES
            NullModule.cppm)

