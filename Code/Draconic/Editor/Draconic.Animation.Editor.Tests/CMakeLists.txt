# Draconic::AnimationEditor unit tests (doctest; cook -> load round-trip, no GPU).

add_executable(Draconic.Animation.Editor.Tests
    TestsMain.cpp
    AnimationEditorTests.cpp)

target_link_libraries(Draconic.Animation.Editor.Tests PRIVATE
    Draconic::AnimationEditor Draconic::AnimationResource Draconic::VFS doctest::doctest)

target_compile_definitions(Draconic.Animation.Editor.Tests PRIVATE
    DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS)

target_compile_options(Draconic.Animation.Editor.Tests PRIVATE
    "$<$<CXX_COMPILER_ID:Clang>:-Wno-error=#warnings;-Wno-error=c2y-extensions>"
    "$<$<CXX_COMPILER_ID:GNU>:-Wno-error=cpp>")

add_test(NAME Draconic.Animation.Editor.Tests COMMAND Draconic.Animation.Editor.Tests)
