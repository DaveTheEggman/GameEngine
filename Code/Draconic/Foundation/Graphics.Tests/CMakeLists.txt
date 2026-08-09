# Foundation::Graphics unit tests (doctest, headless via the Null RHI backend).

add_executable(Graphics.Tests
    TestsMain.cpp
    GraphicsTests.cpp)

target_link_libraries(Graphics.Tests PRIVATE
    Foundation::Graphics Foundation::Graphics.Null Foundation::Shell.Null doctest::doctest)

target_compile_definitions(Graphics.Tests PRIVATE
    DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS)

target_compile_options(Graphics.Tests PRIVATE
    "$<$<CXX_COMPILER_ID:Clang>:-Wno-error=#warnings;-Wno-error=c2y-extensions>"
    "$<$<CXX_COMPILER_ID:GNU>:-Wno-error=cpp>")

add_test(NAME Graphics.Tests COMMAND Graphics.Tests)
