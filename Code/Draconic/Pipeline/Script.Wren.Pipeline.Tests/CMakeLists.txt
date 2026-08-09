# The tests REGISTER the Wren cook (an entry point's job) - the library under test stays
# backend-neutral and resolves through the registry (B3).
add_executable(Script.Wren.Pipeline.Tests TestsMain.cpp WrenCookTests.cpp WrenEditorUITests.cpp)
target_link_libraries(Script.Wren.Pipeline.Tests PRIVATE
    Pipeline::Script.Wren Editor::Script.Wren
    Foundation::Script.Wren Pipeline::Script
    Foundation::Script.Resource Foundation::Script Foundation::Script.Facades
    Foundation::UI.Toolkit Foundation::UI
    doctest::doctest)
target_compile_definitions(Script.Wren.Pipeline.Tests PRIVATE DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS)
target_compile_options(Script.Wren.Pipeline.Tests PRIVATE
    "$<$<CXX_COMPILER_ID:Clang>:-Wno-error=#warnings;-Wno-error=c2y-extensions>"
    "$<$<CXX_COMPILER_ID:GNU>:-Wno-error=cpp>")
add_test(NAME Script.Wren.Pipeline.Tests COMMAND Script.Wren.Pipeline.Tests)
