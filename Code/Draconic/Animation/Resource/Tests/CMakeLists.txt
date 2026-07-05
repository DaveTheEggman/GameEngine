# Draconic::AnimationResource unit tests (doctest; content DB round-trip - no GPU).

add_executable(DraconicAnimationResourceTests
    TestsMain.cpp
    AnimationResourceTests.cpp)

target_link_libraries(DraconicAnimationResourceTests PRIVATE
    Draconic::AnimationResource Draconic::VFS doctest::doctest)

target_compile_definitions(DraconicAnimationResourceTests PRIVATE
    DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS)

target_compile_options(DraconicAnimationResourceTests PRIVATE
    "$<$<CXX_COMPILER_ID:Clang>:-Wno-error=#warnings;-Wno-error=c2y-extensions>"
    "$<$<CXX_COMPILER_ID:GNU>:-Wno-error=cpp>")

add_test(NAME DraconicAnimationResourceTests COMMAND DraconicAnimationResourceTests)
