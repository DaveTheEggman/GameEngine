
add_executable(DraconicVGRendererTests TestsMain.cpp VGRendererTests.cpp)
target_link_libraries(DraconicVGRendererTests PRIVATE
    Draconic::VGRenderer Draconic::VG Draconic::RHI Draconic::RHINull Draconic::Image Draconic::Core doctest::doctest)
target_compile_definitions(DraconicVGRendererTests PRIVATE DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS)
target_compile_options(DraconicVGRendererTests PRIVATE
    "$<$<CXX_COMPILER_ID:Clang>:-Wno-error=#warnings>" "$<$<CXX_COMPILER_ID:GNU>:-Wno-error=cpp>")
add_test(NAME DraconicVGRendererTests COMMAND DraconicVGRendererTests)
