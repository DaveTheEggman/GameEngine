add_executable(DraconicUIShellTests
    UiInputBridgeTests.cpp
    TestsMain.cpp)

target_link_libraries(DraconicUIShellTests PRIVATE
    Draconic::UIShell Draconic::UI Draconic::Shell Draconic::ShellNull doctest::doctest)

target_compile_definitions(DraconicUIShellTests PRIVATE
    DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS)

target_compile_options(DraconicUIShellTests PRIVATE
    "$<$<CXX_COMPILER_ID:Clang>:-Wno-error=#warnings;-Wno-error=c2y-extensions>"
    "$<$<CXX_COMPILER_ID:GNU>:-Wno-error=cpp>")

add_test(NAME DraconicUIShellTests COMMAND DraconicUIShellTests)
