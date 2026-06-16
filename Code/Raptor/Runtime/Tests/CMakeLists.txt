# Raptor::Runtime unit tests (doctest).
if(NOT TARGET doctest::doctest)
    include(FetchContent)
    FetchContent_Declare(doctest
        GIT_REPOSITORY https://github.com/doctest/doctest.git
        GIT_TAG v2.4.11
        GIT_SHALLOW TRUE
        SYSTEM)
    set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
    FetchContent_MakeAvailable(doctest)
    unset(CMAKE_POLICY_VERSION_MINIMUM)
endif()

# A real plugin shared object, dlopen'd by the PluginHost::Load test.
add_library(RaptorRuntimeTestPlugin MODULE TestPlugin.cpp)
set_target_properties(RaptorRuntimeTestPlugin PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_link_libraries(RaptorRuntimeTestPlugin PRIVATE Raptor::Runtime Raptor::Policy)

add_executable(RaptorRuntimeTests
    TestsMain.cpp
    RuntimeTests.cpp)

target_link_libraries(RaptorRuntimeTests PRIVATE Raptor::Runtime doctest::doctest)
add_dependencies(RaptorRuntimeTests RaptorRuntimeTestPlugin)

target_compile_definitions(RaptorRuntimeTests PRIVATE
    DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS
    RAPTOR_TEST_PLUGIN_PATH="$<TARGET_FILE:RaptorRuntimeTestPlugin>")

target_compile_options(RaptorRuntimeTests PRIVATE
    "$<$<CXX_COMPILER_ID:Clang>:-Wno-error=#warnings>"
    "$<$<CXX_COMPILER_ID:GNU>:-Wno-error=cpp>")

add_test(NAME RaptorRuntimeTests COMMAND RaptorRuntimeTests)
