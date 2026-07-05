# Draconic::Animation unit tests (doctest; pure logic over Core math).

add_executable(DraconicAnimationTests
    TestsMain.cpp
    EasingTypeTests.cpp
    PoseTests.cpp
    SkeletonTests.cpp
    ClipTests.cpp
    SamplerTests.cpp
    PlayerTests.cpp
    GraphTests.cpp)

target_link_libraries(DraconicAnimationTests PRIVATE
    Draconic::Animation doctest::doctest)

target_compile_definitions(DraconicAnimationTests PRIVATE
    DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS)

target_compile_options(DraconicAnimationTests PRIVATE
    "$<$<CXX_COMPILER_ID:Clang>:-Wno-error=#warnings;-Wno-error=c2y-extensions>"
    "$<$<CXX_COMPILER_ID:GNU>:-Wno-error=cpp>")

add_test(NAME DraconicAnimationTests COMMAND DraconicAnimationTests)
