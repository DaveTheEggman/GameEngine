
add_executable(DraconicVGTests TestsMain.cpp VGCoreTests.cpp VGPathTests.cpp VGTessellationTests.cpp VGBatchCacheTests.cpp VGContextTests.cpp
    # Ports of Sedulous.VG.Tests (inherit upstream coverage):
    VGCurveUtilsTests.cpp VGStyleTests.cpp VGTriangulatorTests.cpp VGDashGeneratorTests.cpp
    VGPathPortTests.cpp VGPathBuilderTests.cpp VGShapeBuilderTests.cpp
    VGFillTessellatorTests.cpp VGStrokeTessellatorTests.cpp
    VGPathCacheTests.cpp VGContextPortTests.cpp)
target_link_libraries(DraconicVGTests PRIVATE Draconic::VG Draconic::Core Draconic::Image Draconic::Fonts doctest::doctest)
target_compile_definitions(DraconicVGTests PRIVATE DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS)
target_compile_options(DraconicVGTests PRIVATE
    "$<$<CXX_COMPILER_ID:Clang>:-Wno-error=#warnings>" "$<$<CXX_COMPILER_ID:GNU>:-Wno-error=cpp>")
add_test(NAME DraconicVGTests COMMAND DraconicVGTests)
