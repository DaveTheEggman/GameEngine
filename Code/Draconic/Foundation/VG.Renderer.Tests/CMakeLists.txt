
add_executable(VG.Renderer.Tests TestsMain.cpp VGRendererTests.cpp)
target_link_libraries(VG.Renderer.Tests PRIVATE
    Foundation::VG.Renderer Foundation::VG Foundation::RHI Foundation::RHI.Null Foundation::Image Foundation::Core doctest::doctest)
target_compile_definitions(VG.Renderer.Tests PRIVATE DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS)
target_compile_options(VG.Renderer.Tests PRIVATE
    "$<$<CXX_COMPILER_ID:Clang>:-Wno-error=#warnings>" "$<$<CXX_COMPILER_ID:GNU>:-Wno-error=cpp>")
add_test(NAME VG.Renderer.Tests COMMAND VG.Renderer.Tests)
