# Draconic::Animation.Pipeline unit tests (doctest; cook -> load round-trip, no GPU).

add_executable(Draconic.Animation.Pipeline.Tests
    TestsMain.cpp
    AnimationEditorTests.cpp)

target_link_libraries(Draconic.Animation.Pipeline.Tests PRIVATE
    Draconic::Animation.Pipeline Draconic::Animation.Resource Draconic::VFS doctest::doctest)

target_compile_definitions(Draconic.Animation.Pipeline.Tests PRIVATE
    DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS)

target_compile_options(Draconic.Animation.Pipeline.Tests PRIVATE
    "$<$<CXX_COMPILER_ID:Clang>:-Wno-error=#warnings;-Wno-error=c2y-extensions>"
    "$<$<CXX_COMPILER_ID:GNU>:-Wno-error=cpp>")

add_test(NAME Draconic.Animation.Pipeline.Tests COMMAND Draconic.Animation.Pipeline.Tests)
