add_executable(UI.Viewport.Tests TestsMain.cpp ViewportViewTests.cpp)
target_link_libraries(UI.Viewport.Tests PRIVATE
    Foundation::UI.Viewport Foundation::UI Foundation::VG.Renderer Foundation::VG Foundation::Image
    Foundation::RHI Foundation::RHI.Null Foundation::Shell Foundation::Shell.Null Foundation::Core doctest::doctest)
target_compile_definitions(UI.Viewport.Tests PRIVATE DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS)
target_compile_options(UI.Viewport.Tests PRIVATE
    "$<$<CXX_COMPILER_ID:Clang>:-Wno-error=#warnings>" "$<$<CXX_COMPILER_ID:GNU>:-Wno-error=cpp>")
add_test(NAME UI.Viewport.Tests COMMAND UI.Viewport.Tests)
