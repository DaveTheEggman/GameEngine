# Draconic::GUIShell tests (doctest).
add_executable(Draconic.GUI.Shell.Tests
    TestsMain.cpp
    BridgeTests.cpp)

target_link_libraries(Draconic.GUI.Shell.Tests PRIVATE
    Draconic::GUIShell Draconic::ShellNull doctest::doctest)

target_compile_definitions(Draconic.GUI.Shell.Tests PRIVATE
    DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS)

target_compile_options(Draconic.GUI.Shell.Tests PRIVATE
    "$<$<CXX_COMPILER_ID:Clang>:-Wno-error=#warnings;-Wno-error=c2y-extensions>"
    "$<$<CXX_COMPILER_ID:GNU>:-Wno-error=cpp>")

add_test(NAME Draconic.GUI.Shell.Tests COMMAND Draconic.GUI.Shell.Tests)
