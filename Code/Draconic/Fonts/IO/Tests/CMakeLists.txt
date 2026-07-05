# Draconic::FontsIO unit tests (doctest).

add_executable(DraconicFontsIOTests TestsMain.cpp FontIOTests.cpp)
target_link_libraries(DraconicFontsIOTests PRIVATE Draconic::FontsIO Draconic::FontsBaked Draconic::Fonts doctest::doctest)
target_compile_definitions(DraconicFontsIOTests PRIVATE DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS)
target_compile_options(DraconicFontsIOTests PRIVATE
    "$<$<CXX_COMPILER_ID:Clang>:-Wno-error=#warnings;-Wno-error=c2y-extensions>" "$<$<CXX_COMPILER_ID:GNU>:-Wno-error=cpp>")
add_test(NAME DraconicFontsIOTests COMMAND DraconicFontsIOTests)
