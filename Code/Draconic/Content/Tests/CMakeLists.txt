# Draconic::Content unit tests (doctest).

add_executable(DraconicContentTests TestsMain.cpp ContentTests.cpp)
target_link_libraries(DraconicContentTests PRIVATE Draconic::Content Draconic::XmlSerialization doctest::doctest)
target_compile_definitions(DraconicContentTests PRIVATE DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS)
target_compile_options(DraconicContentTests PRIVATE
    "$<$<CXX_COMPILER_ID:Clang>:-Wno-error=#warnings;-Wno-error=c2y-extensions>" "$<$<CXX_COMPILER_ID:GNU>:-Wno-error=cpp>")
add_test(NAME DraconicContentTests COMMAND DraconicContentTests)
