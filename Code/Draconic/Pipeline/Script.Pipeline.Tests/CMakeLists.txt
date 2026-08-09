# The tests REGISTER the Wren backend (an entry point's job) - the library under test
# stays backend-neutral and resolves through the registry (B3).
add_executable(Script.Pipeline.Tests TestsMain.cpp ScriptPipelineTests.cpp ScriptReflectionTests.cpp)
target_link_libraries(Script.Pipeline.Tests PRIVATE
    Pipeline::Script Pipeline::Script.Wren Foundation::Script.Resource Foundation::Script
    Foundation::Script.Wren Foundation::Script.Facades
    Pipeline::Core Editor::Core Foundation::Content Foundation::Resource Foundation::VFS
    doctest::doctest)
target_compile_definitions(Script.Pipeline.Tests PRIVATE DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS)
target_compile_options(Script.Pipeline.Tests PRIVATE
    "$<$<CXX_COMPILER_ID:Clang>:-Wno-error=#warnings;-Wno-error=c2y-extensions>"
    "$<$<CXX_COMPILER_ID:GNU>:-Wno-error=cpp>")
add_test(NAME Script.Pipeline.Tests COMMAND Script.Pipeline.Tests)
