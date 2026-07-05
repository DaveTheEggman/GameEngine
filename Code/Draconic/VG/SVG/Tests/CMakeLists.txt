
add_executable(DraconicVGSVGTests TestsMain.cpp
    SVGColorParserTests.cpp SVGTransformParserTests.cpp SVGPathParserTests.cpp
    SVGLoaderTests.cpp)
target_link_libraries(DraconicVGSVGTests PRIVATE Draconic::VGSVG Draconic::VG Draconic::Core doctest::doctest)
target_compile_definitions(DraconicVGSVGTests PRIVATE DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS)
target_compile_options(DraconicVGSVGTests PRIVATE
    "$<$<CXX_COMPILER_ID:Clang>:-Wno-error=#warnings>" "$<$<CXX_COMPILER_ID:GNU>:-Wno-error=cpp>")
add_test(NAME DraconicVGSVGTests COMMAND DraconicVGSVGTests)
