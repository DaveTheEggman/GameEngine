# The tests REGISTER the Wren cook (an entry point's job) - the library under test stays
# backend-neutral and resolves through the registry (B3).
add_executable(DraconicWrenCookTests TestsMain.cpp WrenCookTests.cpp WrenEditorUITests.cpp)
target_link_libraries(DraconicWrenCookTests PRIVATE
    Draconic::Script.Wren.Pipeline Draconic::Editor.Script.Wren
    Draconic::Script.Wren Draconic::Script.Pipeline
    Draconic::Script.Resource Draconic::Script Draconic::Script.Facades
    Draconic::UI.Toolkit Draconic::UI
    doctest::doctest)
target_compile_definitions(DraconicWrenCookTests PRIVATE DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS)
target_compile_options(DraconicWrenCookTests PRIVATE
    "$<$<CXX_COMPILER_ID:Clang>:-Wno-error=#warnings;-Wno-error=c2y-extensions>"
    "$<$<CXX_COMPILER_ID:GNU>:-Wno-error=cpp>")
add_test(NAME DraconicWrenCookTests COMMAND DraconicWrenCookTests)
