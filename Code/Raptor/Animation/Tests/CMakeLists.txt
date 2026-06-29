# Raptor::Animation unit tests (doctest; pure logic over Core math).
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

add_executable(RaptorAnimationTests
    TestsMain.cpp
    EasingTypeTests.cpp
    PoseTests.cpp
    SkeletonTests.cpp
    ClipTests.cpp
    SamplerTests.cpp)

target_link_libraries(RaptorAnimationTests PRIVATE
    Raptor::Animation doctest::doctest)

target_compile_definitions(RaptorAnimationTests PRIVATE
    DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS)

target_compile_options(RaptorAnimationTests PRIVATE
    "$<$<CXX_COMPILER_ID:Clang>:-Wno-error=#warnings;-Wno-error=c2y-extensions>"
    "$<$<CXX_COMPILER_ID:GNU>:-Wno-error=cpp>")

add_test(NAME RaptorAnimationTests COMMAND RaptorAnimationTests)
