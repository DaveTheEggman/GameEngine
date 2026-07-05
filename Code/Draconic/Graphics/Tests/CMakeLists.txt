# Draconic::Graphics unit tests (doctest, headless via the Null RHI backend).

add_executable(DraconicGraphicsTests
    TestsMain.cpp
    GraphicsTests.cpp)

target_link_libraries(DraconicGraphicsTests PRIVATE
    Draconic::Graphics Draconic::GraphicsNull Draconic::ShellNull doctest::doctest)

target_compile_definitions(DraconicGraphicsTests PRIVATE
    DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS)

target_compile_options(DraconicGraphicsTests PRIVATE
    "$<$<CXX_COMPILER_ID:Clang>:-Wno-error=#warnings;-Wno-error=c2y-extensions>"
    "$<$<CXX_COMPILER_ID:GNU>:-Wno-error=cpp>")

add_test(NAME DraconicGraphicsTests COMMAND DraconicGraphicsTests)
