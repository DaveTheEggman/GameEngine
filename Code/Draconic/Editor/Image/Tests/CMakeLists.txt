add_executable(DraconicEditorImageTests TestsMain.cpp ImageEditorPageTests.cpp)
target_link_libraries(DraconicEditorImageTests PRIVATE
    Draconic::EditorImage Draconic::EditorCore
    Draconic::Image Draconic::ImageEditor Draconic::Content doctest::doctest)
target_compile_definitions(DraconicEditorImageTests PRIVATE DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS)
target_compile_options(DraconicEditorImageTests PRIVATE
    "$<$<CXX_COMPILER_ID:Clang>:-Wno-error=#warnings>" "$<$<CXX_COMPILER_ID:GNU>:-Wno-error=cpp>")
add_test(NAME DraconicEditorImageTests COMMAND DraconicEditorImageTests)
