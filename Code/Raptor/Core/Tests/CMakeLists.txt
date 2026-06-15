# Raptor::Core unit tests (doctest).
#
# doctest is configured for -fno-exceptions (DOCTEST_CONFIG_NO_EXCEPTIONS_*),
# matching the engine-wide policy propagated via Raptor::Core -> Raptor::Policy.

include(FetchContent)
FetchContent_Declare(doctest
    GIT_REPOSITORY https://github.com/doctest/doctest.git
    GIT_TAG v2.4.11
    GIT_SHALLOW TRUE
    SYSTEM) # third-party: don't apply our -Wall/-Werror to doctest headers
# doctest 2.4.11 ships a pre-3.5 cmake_minimum_required that CMake 4+ rejects.
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
FetchContent_MakeAvailable(doctest)
unset(CMAKE_POLICY_VERSION_MINIMUM)

add_executable(RaptorCoreTests CoreTests.cpp)

target_link_libraries(RaptorCoreTests PRIVATE Raptor::Core doctest::doctest)

target_compile_definitions(RaptorCoreTests PRIVATE
    DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS
    DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN)

# doctest 2.4.11 includes <ciso646>, which emits a deprecation #warning under
# C++23. It's third-party noise — de-promote just that warning for this target;
# our own code keeps -Werror.
target_compile_options(RaptorCoreTests PRIVATE
    "$<$<CXX_COMPILER_ID:Clang>:-Wno-error=#warnings>"
    "$<$<CXX_COMPILER_ID:GNU>:-Wno-error=cpp>")

add_test(NAME RaptorCoreTests COMMAND RaptorCoreTests)
