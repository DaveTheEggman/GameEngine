# Draconic::FontsBaked unit tests (doctest).

add_executable(DraconicFontsBakedTests TestsMain.cpp BakedFontTests.cpp)
target_link_libraries(DraconicFontsBakedTests PRIVATE Draconic::FontsBaked Draconic::Fonts doctest::doctest)
target_compile_definitions(DraconicFontsBakedTests PRIVATE DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS)
target_compile_options(DraconicFontsBakedTests PRIVATE
    "$<$<CXX_COMPILER_ID:Clang>:-Wno-error=#warnings;-Wno-error=c2y-extensions>" "$<$<CXX_COMPILER_ID:GNU>:-Wno-error=cpp>")
add_test(NAME DraconicFontsBakedTests COMMAND DraconicFontsBakedTests)
