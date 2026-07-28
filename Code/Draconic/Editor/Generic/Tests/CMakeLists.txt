add_executable(DraconicEditorGenericTests TestsMain.cpp AssetFormTests.cpp)
target_link_libraries(DraconicEditorGenericTests PRIVATE
    Draconic::EditorGeneric Draconic::EditorCore Draconic::Content doctest::doctest)
target_compile_definitions(DraconicEditorGenericTests PRIVATE DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS)
target_compile_options(DraconicEditorGenericTests PRIVATE
    "$<$<CXX_COMPILER_ID:Clang>:-Wno-error=#warnings>" "$<$<CXX_COMPILER_ID:GNU>:-Wno-error=cpp>")
add_test(NAME DraconicEditorGenericTests COMMAND DraconicEditorGenericTests)
