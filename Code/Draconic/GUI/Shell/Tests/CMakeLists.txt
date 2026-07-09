# Draconic::GUIShell tests (doctest).
add_executable(DraconicGUIShellTests
    TestsMain.cpp
    BridgeTests.cpp)

target_link_libraries(DraconicGUIShellTests PRIVATE
    Draconic::GUIShell doctest::doctest)

target_compile_definitions(DraconicGUIShellTests PRIVATE
    DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS)

target_compile_options(DraconicGUIShellTests PRIVATE
    "$<$<CXX_COMPILER_ID:Clang>:-Wno-error=#warnings;-Wno-error=c2y-extensions>"
    "$<$<CXX_COMPILER_ID:GNU>:-Wno-error=cpp>")

add_test(NAME DraconicGUIShellTests COMMAND DraconicGUIShellTests)
