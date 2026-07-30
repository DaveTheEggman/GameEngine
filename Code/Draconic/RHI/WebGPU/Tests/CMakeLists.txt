# Draconic::RHIWebGPU unit tests (doctest). Environment-dependent: they skip
# (vacuously pass) when the wgpu-native sidecar or a GPU is unavailable.

add_executable(DraconicRHIWebGPUTests
    TestsMain.cpp
    WebGpuRhiTests.cpp)

target_link_libraries(DraconicRHIWebGPUTests PRIVATE Draconic::RHIWebGPU doctest::doctest)

target_compile_definitions(DraconicRHIWebGPUTests PRIVATE
    DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS)

target_compile_options(DraconicRHIWebGPUTests PRIVATE
    "$<$<CXX_COMPILER_ID:Clang>:-Wno-error=#warnings;-Wno-error=c2y-extensions>"
    "$<$<CXX_COMPILER_ID:GNU>:-Wno-error=cpp>")

add_test(NAME DraconicRHIWebGPUTests COMMAND DraconicRHIWebGPUTests)
