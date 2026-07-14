add_executable(DraconicEditorSceneTests TestsMain.cpp SceneEditorTests.cpp EditContextTests.cpp GizmoTests.cpp)
target_link_libraries(DraconicEditorSceneTests PRIVATE Draconic::EditorScene Draconic::EditorCore Draconic::Content Draconic::RenderSubsystem Draconic::Materials Draconic::MaterialEditor doctest::doctest)
target_compile_definitions(DraconicEditorSceneTests PRIVATE DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS)
target_compile_options(DraconicEditorSceneTests PRIVATE
    "$<$<CXX_COMPILER_ID:Clang>:-Wno-error=#warnings>" "$<$<CXX_COMPILER_ID:GNU>:-Wno-error=cpp>")
add_test(NAME DraconicEditorSceneTests COMMAND DraconicEditorSceneTests)
