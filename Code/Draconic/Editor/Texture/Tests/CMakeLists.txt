add_executable(DraconicEditorTextureTests TestsMain.cpp TextureEditorPageTests.cpp)
target_link_libraries(DraconicEditorTextureTests PRIVATE
    Draconic::EditorTexture Draconic::EditorCore
    Draconic::Texture Draconic::TextureEditor Draconic::Image Draconic::ImageIO
    Draconic::Content Draconic::VFS doctest::doctest)
target_compile_definitions(DraconicEditorTextureTests PRIVATE DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS)
target_compile_options(DraconicEditorTextureTests PRIVATE
    "$<$<CXX_COMPILER_ID:Clang>:-Wno-error=#warnings>" "$<$<CXX_COMPILER_ID:GNU>:-Wno-error=cpp>")
add_test(NAME DraconicEditorTextureTests COMMAND DraconicEditorTextureTests)
