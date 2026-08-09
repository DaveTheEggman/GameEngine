# Foundation::RHI.Null - `foundation.rhi.null` (headless no-op RHI backend, for tests/tools).

add_library(RHI.Null STATIC)
add_library(Foundation::RHI.Null ALIAS RHI.Null)

target_link_libraries(RHI.Null PUBLIC Foundation::RHI Foundation::Core Foundation::Policy)

target_sources(RHI.Null
    PUBLIC
        FILE_SET rhi_null_modules TYPE CXX_MODULES FILES
            NullModule.cppm)

