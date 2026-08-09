# Foundation::Shaders unit tests (doctest). Exercises the real DXC runtime.

add_executable(Shaders.Tests
    TestsMain.cpp
    ShadersTests.cpp)

target_link_libraries(Shaders.Tests PRIVATE Foundation::Shaders doctest::doctest)

target_compile_definitions(Shaders.Tests PRIVATE
    DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS
    # Lets the cook test run over the real engine corpus (same define the renderer uses).
    DRACONIC_ENGINE_SHADER_DIR="${CMAKE_SOURCE_DIR}/Data/Shaders")

target_compile_options(Shaders.Tests PRIVATE
    "$<$<CXX_COMPILER_ID:Clang>:-Wno-error=#warnings;-Wno-error=c2y-extensions>"
    "$<$<CXX_COMPILER_ID:GNU>:-Wno-error=cpp>")

add_test(NAME Shaders.Tests COMMAND Shaders.Tests)
