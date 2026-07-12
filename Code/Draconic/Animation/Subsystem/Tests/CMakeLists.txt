# Draconic::AnimationSubsystem unit tests (doctest; components are pure data, no GPU).

add_executable(DraconicAnimationSubsystemTests
    ResourceRefTests.cpp
    TestsMain.cpp)

target_link_libraries(DraconicAnimationSubsystemTests PRIVATE
    Draconic::AnimationSubsystem Draconic::AnimationResource Draconic::SceneResource
    Draconic::Resource Draconic::VFS Draconic::Content doctest::doctest)

target_compile_definitions(DraconicAnimationSubsystemTests PRIVATE
    DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS)

target_compile_options(DraconicAnimationSubsystemTests PRIVATE
    "$<$<CXX_COMPILER_ID:Clang>:-Wno-error=#warnings;-Wno-error=c2y-extensions>"
    "$<$<CXX_COMPILER_ID:GNU>:-Wno-error=cpp>")

add_test(NAME DraconicAnimationSubsystemTests COMMAND DraconicAnimationSubsystemTests)
