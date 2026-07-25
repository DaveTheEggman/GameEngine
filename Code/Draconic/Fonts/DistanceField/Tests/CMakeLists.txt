# Draconic::FontsDF unit tests (doctest).

add_executable(DraconicFontsDFTests TestsMain.cpp DFFontAtlasTests.cpp)
target_link_libraries(DraconicFontsDFTests PRIVATE Draconic::FontsDF Draconic::Fonts doctest::doctest)
target_compile_definitions(DraconicFontsDFTests PRIVATE DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS)
target_compile_options(DraconicFontsDFTests PRIVATE
    "$<$<CXX_COMPILER_ID:Clang>:-Wno-error=#warnings;-Wno-error=c2y-extensions>" "$<$<CXX_COMPILER_ID:GNU>:-Wno-error=cpp>")
add_test(NAME DraconicFontsDFTests COMMAND DraconicFontsDFTests)
