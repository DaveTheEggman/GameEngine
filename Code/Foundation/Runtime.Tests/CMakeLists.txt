# Foundation::Runtime unit tests (doctest).

# A real plugin shared object, dlopen'd by the PluginHost::Load test.
add_library(Runtime.TestPlugin MODULE TestPlugin.cpp)
set_target_properties(Runtime.TestPlugin PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_link_libraries(Runtime.TestPlugin PRIVATE Foundation::Runtime Foundation::Policy)

add_executable(Runtime.Tests
    TestsMain.cpp
    RuntimeTests.cpp)

target_link_libraries(Runtime.Tests PRIVATE Foundation::Runtime Foundation::Runtime.Client doctest::doctest)
add_dependencies(Runtime.Tests Runtime.TestPlugin)

target_compile_definitions(Runtime.Tests PRIVATE
    DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS
    TEST_PLUGIN_PATH="$<TARGET_FILE:Runtime.TestPlugin>")

target_compile_options(Runtime.Tests PRIVATE
    "$<$<CXX_COMPILER_ID:Clang>:-Wno-error=#warnings;-Wno-error=c2y-extensions>"
    "$<$<CXX_COMPILER_ID:GNU>:-Wno-error=cpp>")

add_test(NAME Runtime.Tests COMMAND Runtime.Tests)
