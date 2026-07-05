
add_executable(DraconicEditorTests TestsMain.cpp EditorTests.cpp)
target_link_libraries(DraconicEditorTests PRIVATE Draconic::Editor Draconic::Content doctest::doctest)
target_compile_definitions(DraconicEditorTests PRIVATE DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS)
target_compile_options(DraconicEditorTests PRIVATE
    "$<$<CXX_COMPILER_ID:Clang>:-Wno-error=#warnings>" "$<$<CXX_COMPILER_ID:GNU>:-Wno-error=cpp>")
add_test(NAME DraconicEditorTests COMMAND DraconicEditorTests)
