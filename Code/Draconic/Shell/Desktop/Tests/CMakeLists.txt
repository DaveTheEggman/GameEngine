# Draconic::ShellDesktop unit tests (doctest). Force SDL's dummy video
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

add_executable(DraconicShellDesktopTests
    TestsMain.cpp
    SDL3ShellTests.cpp)

# The test TU includes <SDL3/SDL.h> directly to push synthetic events.
target_link_libraries(DraconicShellDesktopTests PRIVATE
    Draconic::ShellDesktop Draconic::RuntimeClient Draconic::RuntimeDesktop SDL3::SDL3 doctest::doctest)

target_compile_definitions(DraconicShellDesktopTests PRIVATE
    DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS)

target_compile_options(DraconicShellDesktopTests PRIVATE
    "$<$<CXX_COMPILER_ID:Clang>:-Wno-error=#warnings;-Wno-error=c2y-extensions>"
    "$<$<CXX_COMPILER_ID:GNU>:-Wno-error=cpp>")

add_test(NAME DraconicShellDesktopTests COMMAND DraconicShellDesktopTests)

# Copy SDL3.dll next to the test executable on Windows.
if(DRACONIC_SDL3_DLL)
    add_custom_command(TARGET DraconicShellDesktopTests POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${DRACONIC_SDL3_DLL}" "$<TARGET_FILE_DIR:DraconicShellDesktopTests>")
endif()
