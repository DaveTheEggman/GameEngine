# Draconic::UI unit tests (doctest). Ports of Sedulous.UI.Tests land here as the port grows.
add_executable(DraconicUITests
    TestsMain.cpp
    UICoreTests.cpp)

target_link_libraries(DraconicUITests PRIVATE
    Draconic::UI doctest::doctest)

target_compile_definitions(DraconicUITests PRIVATE
    DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS)

target_compile_options(DraconicUITests PRIVATE
    "$<$<CXX_COMPILER_ID:Clang>:-Wno-error=#warnings;-Wno-error=c2y-extensions>"
    "$<$<CXX_COMPILER_ID:GNU>:-Wno-error=cpp>")

add_test(NAME DraconicUITests COMMAND DraconicUITests)
