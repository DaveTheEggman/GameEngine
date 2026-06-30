# Draconic::RuntimePlatformDesktop unit tests (doctest). Force SDL's dummy video
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

add_executable(DraconicRuntimePlatformDesktopTests
    TestsMain.cpp
    SDL3PlatformTests.cpp)

# The test TU includes <SDL3/SDL.h> directly to push synthetic events.
target_link_libraries(DraconicRuntimePlatformDesktopTests PRIVATE
    Draconic::RuntimePlatformDesktop Draconic::RuntimeClient SDL3::SDL3 doctest::doctest)

target_compile_definitions(DraconicRuntimePlatformDesktopTests PRIVATE
    DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS)

target_compile_options(DraconicRuntimePlatformDesktopTests PRIVATE
    "$<$<CXX_COMPILER_ID:Clang>:-Wno-error=#warnings;-Wno-error=c2y-extensions>"
    "$<$<CXX_COMPILER_ID:GNU>:-Wno-error=cpp>")

add_test(NAME DraconicRuntimePlatformDesktopTests COMMAND DraconicRuntimePlatformDesktopTests)

# Copy SDL3.dll next to the test executable on Windows.
if(DRACONIC_SDL3_DLL)
    add_custom_command(TARGET DraconicRuntimePlatformDesktopTests POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${DRACONIC_SDL3_DLL}" "$<TARGET_FILE_DIR:DraconicRuntimePlatformDesktopTests>")
endif()
