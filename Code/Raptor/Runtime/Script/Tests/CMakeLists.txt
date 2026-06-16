# Raptor::RuntimeScript unit tests (doctest). Uses the Wren backend as the
# injected script manager.
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

add_executable(RaptorRuntimeScriptTests
    TestsMain.cpp
    ScriptSubsystemTests.cpp)

target_link_libraries(RaptorRuntimeScriptTests PRIVATE
    Raptor::RuntimeScript Raptor::ScriptWren doctest::doctest)

target_compile_definitions(RaptorRuntimeScriptTests PRIVATE
    DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS)

target_compile_options(RaptorRuntimeScriptTests PRIVATE
    "$<$<CXX_COMPILER_ID:Clang>:-Wno-error=#warnings>"
    "$<$<CXX_COMPILER_ID:GNU>:-Wno-error=cpp>")

add_test(NAME RaptorRuntimeScriptTests COMMAND RaptorRuntimeScriptTests)
