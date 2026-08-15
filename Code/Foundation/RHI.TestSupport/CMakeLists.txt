# Foundation::RHI.TestSupport - `foundation.rhi.testsupport`. A backend-agnostic offscreen-render
# test substrate (make device + read a target back to a probeable CPU image). Test-only helper;
# depends only on RHI + Core so any backend test can link it.

add_library(RHI.TestSupport STATIC)
add_library(Foundation::RHI.TestSupport ALIAS RHI.TestSupport)

target_link_libraries(RHI.TestSupport PUBLIC Foundation::RHI Foundation::Core)

target_sources(RHI.TestSupport
    PUBLIC
        FILE_SET rhi_testsupport_modules TYPE CXX_MODULES FILES
            RhiTestSupport.cppm)
