# The tests REGISTER the Wren backend (an entry point's job) - the provider under test stays
# backend-neutral and resolves through the registry, exactly like the ScriptPage.
add_executable(Editor.Script.Tests TestsMain.cpp ScriptCompletionTests.cpp ScriptApiBrowserTests.cpp)
target_link_libraries(Editor.Script.Tests PRIVATE
    Editor::Script Foundation::Script.Wren Foundation::Script Foundation::Script.Facades
    Foundation::UI.Toolkit Foundation::UI Foundation::Core
    doctest::doctest)
target_compile_definitions(Editor.Script.Tests PRIVATE DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS)
target_compile_options(Editor.Script.Tests PRIVATE
    "$<$<CXX_COMPILER_ID:Clang>:-Wno-error=#warnings;-Wno-error=c2y-extensions>"
    "$<$<CXX_COMPILER_ID:GNU>:-Wno-error=cpp>")
add_test(NAME Editor.Script.Tests COMMAND Editor.Script.Tests)
