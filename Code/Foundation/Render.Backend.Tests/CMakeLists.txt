# Foundation::Render backend-orientation tests: render one asymmetric scene through the FULL
# RenderFrame chain on REAL backends (Vulkan + WebGPU) and read the pixels back - the pixel-level
# ground truth for the Y-flip bug class (each backend must put the cube on the same side of the
# screen in every TAA mode). Skips cleanly when a backend/GPU is unavailable.

add_executable(Render.Backend.Tests
    TestsMain.cpp
    BackendOrientationTests.cpp
    MsaaProbeTests.cpp)

target_link_libraries(Render.Backend.Tests PRIVATE
    Foundation::Render Foundation::Materials.PipelineCache Foundation::RHI.TestSupport
    Foundation::RHI.Vulkan Foundation::RHI.WebGPU doctest::doctest)

target_compile_definitions(Render.Backend.Tests PRIVATE
    DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS
    BUILTIN_ENGINE_SHADER_DIR="${CMAKE_SOURCE_DIR}/Data/Shaders")

target_compile_options(Render.Backend.Tests PRIVATE
    "$<$<CXX_COMPILER_ID:Clang>:-Wno-error=#warnings;-Wno-error=c2y-extensions>"
    "$<$<CXX_COMPILER_ID:GNU>:-Wno-error=cpp>")

add_test(NAME Render.Backend.Tests COMMAND Render.Backend.Tests)
