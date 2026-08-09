# Foundation::Shell.Desktop unit tests (doctest). Force SDL's dummy video
# driver at runtime so these are headless/CI-safe.

add_executable(Shell.Desktop.Tests
    TestsMain.cpp
    SDL3ShellTests.cpp)

# The test TU includes <SDL3/SDL.h> directly to push synthetic events.
target_link_libraries(Shell.Desktop.Tests PRIVATE
    Foundation::Shell.Desktop Foundation::Runtime.Client Foundation::Runtime.Desktop SDL3::SDL3 doctest::doctest)

target_compile_definitions(Shell.Desktop.Tests PRIVATE
    DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS)

target_compile_options(Shell.Desktop.Tests PRIVATE
    "$<$<CXX_COMPILER_ID:Clang>:-Wno-error=#warnings;-Wno-error=c2y-extensions>"
    "$<$<CXX_COMPILER_ID:GNU>:-Wno-error=cpp>")

add_test(NAME Shell.Desktop.Tests COMMAND Shell.Desktop.Tests)

# Copy SDL3.dll next to the test executable on Windows.
util_copy_runtime_deps(Shell.Desktop.Tests EXTRA ${BUILDSYSTEM_SDL3_DLL})
