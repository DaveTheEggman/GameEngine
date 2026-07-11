add_executable(DraconicEditorAppTests TestsMain.cpp ShellTests.cpp LogViewTests.cpp)
target_link_libraries(DraconicEditorAppTests PRIVATE Draconic::EditorApp Draconic::EditorCore doctest::doctest)
target_compile_definitions(DraconicEditorAppTests PRIVATE DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS)
target_compile_options(DraconicEditorAppTests PRIVATE
    "$<$<CXX_COMPILER_ID:Clang>:-Wno-error=#warnings>" "$<$<CXX_COMPILER_ID:GNU>:-Wno-error=cpp>")
add_test(NAME DraconicEditorAppTests COMMAND DraconicEditorAppTests)
