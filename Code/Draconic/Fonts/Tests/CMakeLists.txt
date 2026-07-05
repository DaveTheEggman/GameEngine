# Draconic::Fonts unit tests (doctest).

add_executable(DraconicFontsTests TestsMain.cpp FontsTypesTests.cpp)
target_link_libraries(DraconicFontsTests PRIVATE Draconic::Fonts doctest::doctest)
target_compile_definitions(DraconicFontsTests PRIVATE DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS)
target_compile_options(DraconicFontsTests PRIVATE
    "$<$<CXX_COMPILER_ID:Clang>:-Wno-error=#warnings;-Wno-error=c2y-extensions>" "$<$<CXX_COMPILER_ID:GNU>:-Wno-error=cpp>")
add_test(NAME DraconicFontsTests COMMAND DraconicFontsTests)
