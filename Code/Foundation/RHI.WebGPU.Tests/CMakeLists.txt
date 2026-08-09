# Foundation::RHI.WebGPU unit tests (doctest). Environment-dependent: they skip
# (vacuously pass) when the wgpu-native sidecar or a GPU is unavailable.

add_executable(RHI.WebGPU.Tests
    TestsMain.cpp
    WebGpuRhiTests.cpp)

target_link_libraries(RHI.WebGPU.Tests PRIVATE Foundation::RHI.WebGPU doctest::doctest)

target_compile_definitions(RHI.WebGPU.Tests PRIVATE
    DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS)

target_compile_options(RHI.WebGPU.Tests PRIVATE
    "$<$<CXX_COMPILER_ID:Clang>:-Wno-error=#warnings;-Wno-error=c2y-extensions>"
    "$<$<CXX_COMPILER_ID:GNU>:-Wno-error=cpp>")

add_test(NAME RHI.WebGPU.Tests COMMAND RHI.WebGPU.Tests)
