# Draconic::Runtime unit tests (doctest).
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
