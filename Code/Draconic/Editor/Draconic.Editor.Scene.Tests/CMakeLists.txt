add_executable(Draconic.Editor.Scene.Tests TestsMain.cpp SceneEditorTests.cpp EditContextTests.cpp GizmoTests.cpp ModelPrefabTests.cpp MeshPageTests.cpp ParticleEffectPageTests.cpp AnimationGraphPageTests.cpp AnimationGraphCreatorTests.cpp)
target_link_libraries(Draconic.Editor.Scene.Tests PRIVATE Draconic::EditorScene Draconic::EditorCore Draconic::Content Draconic::RenderSubsystem Draconic::AnimationSubsystem Draconic::ModelImporter Draconic::VFS Draconic::Geometry Draconic::Materials Draconic::MaterialEditor Draconic::Particles doctest::doctest)
target_compile_definitions(Draconic.Editor.Scene.Tests PRIVATE DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS)
target_compile_options(Draconic.Editor.Scene.Tests PRIVATE
    "$<$<CXX_COMPILER_ID:Clang>:-Wno-error=#warnings>" "$<$<CXX_COMPILER_ID:GNU>:-Wno-error=cpp>")
add_test(NAME Draconic.Editor.Scene.Tests COMMAND Draconic.Editor.Scene.Tests)
