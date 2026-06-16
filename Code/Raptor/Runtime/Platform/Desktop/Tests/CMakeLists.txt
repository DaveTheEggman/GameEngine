# Raptor::RuntimePlatformDesktop unit tests (doctest). Force SDL's dummy video
# driver at runtime so these are headless/CI-safe.
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

add_executable(RaptorRuntimePlatformDesktopTests
    TestsMain.cpp
    SDL3PlatformTests.cpp)

# The test TU includes <SDL3/SDL.h> directly to push synthetic events.
target_link_libraries(RaptorRuntimePlatformDesktopTests PRIVATE
    Raptor::RuntimePlatformDesktop SDL3::SDL3 doctest::doctest)

target_compile_definitions(RaptorRuntimePlatformDesktopTests PRIVATE
    DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS)

target_compile_options(RaptorRuntimePlatformDesktopTests PRIVATE
    "$<$<CXX_COMPILER_ID:Clang>:-Wno-error=#warnings>"
    "$<$<CXX_COMPILER_ID:GNU>:-Wno-error=cpp>")

add_test(NAME RaptorRuntimePlatformDesktopTests COMMAND RaptorRuntimePlatformDesktopTests)
