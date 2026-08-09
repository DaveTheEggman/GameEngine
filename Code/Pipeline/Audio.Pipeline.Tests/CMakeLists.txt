add_executable(Audio.Pipeline.Tests TestsMain.cpp AudioPipelineTests.cpp AudioReflectionTests.cpp)
target_link_libraries(Audio.Pipeline.Tests PRIVATE
    Pipeline::Audio Foundation::Audio.Resource Foundation::Audio Pipeline::Core
    Editor::Core Foundation::Content Foundation::Resource Foundation::VFS doctest::doctest)
target_compile_definitions(Audio.Pipeline.Tests PRIVATE DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS)
target_compile_options(Audio.Pipeline.Tests PRIVATE
    "$<$<CXX_COMPILER_ID:Clang>:-Wno-error=#warnings;-Wno-error=c2y-extensions>"
    "$<$<CXX_COMPILER_ID:GNU>:-Wno-error=cpp>")
add_test(NAME Audio.Pipeline.Tests COMMAND Audio.Pipeline.Tests)
