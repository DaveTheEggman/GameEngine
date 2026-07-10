add_executable(DraconicUIViewportTests TestsMain.cpp ViewportViewTests.cpp)
target_link_libraries(DraconicUIViewportTests PRIVATE
    Draconic::UIViewport Draconic::UI Draconic::VGRenderer Draconic::VG Draconic::Image
    Draconic::RHI Draconic::RHINull Draconic::Core doctest::doctest)
target_compile_definitions(DraconicUIViewportTests PRIVATE DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS)
target_compile_options(DraconicUIViewportTests PRIVATE
    "$<$<CXX_COMPILER_ID:Clang>:-Wno-error=#warnings>" "$<$<CXX_COMPILER_ID:GNU>:-Wno-error=cpp>")
add_test(NAME DraconicUIViewportTests COMMAND DraconicUIViewportTests)
