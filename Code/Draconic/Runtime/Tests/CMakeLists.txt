# Draconic::Runtime unit tests (doctest).

# A real plugin shared object, dlopen'd by the PluginHost::Load test.
add_library(DraconicRuntimeTestPlugin MODULE TestPlugin.cpp)
set_target_properties(DraconicRuntimeTestPlugin PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_link_libraries(DraconicRuntimeTestPlugin PRIVATE Draconic::Runtime Draconic::Policy)

add_executable(DraconicRuntimeTests
    TestsMain.cpp
    RuntimeTests.cpp)

target_link_libraries(DraconicRuntimeTests PRIVATE Draconic::Runtime doctest::doctest)
add_dependencies(DraconicRuntimeTests DraconicRuntimeTestPlugin)

target_compile_definitions(DraconicRuntimeTests PRIVATE
    DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS
    DRACONIC_TEST_PLUGIN_PATH="$<TARGET_FILE:DraconicRuntimeTestPlugin>")

target_compile_options(DraconicRuntimeTests PRIVATE
    "$<$<CXX_COMPILER_ID:Clang>:-Wno-error=#warnings;-Wno-error=c2y-extensions>"
    "$<$<CXX_COMPILER_ID:GNU>:-Wno-error=cpp>")

add_test(NAME DraconicRuntimeTests COMMAND DraconicRuntimeTests)
