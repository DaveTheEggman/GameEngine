add_executable(Editor.Scene.Tests TestsMain.cpp SceneActionsTests.cpp SceneEditorTests.cpp EditContextTests.cpp GizmoTests.cpp SelectToolTests.cpp ModelPrefabTests.cpp MeshPageTests.cpp ParticleEffectPageTests.cpp AnimationGraphPageTests.cpp AnimationGraphCreatorTests.cpp CameraPreviewTests.cpp CollisionMatrixTests.cpp SceneMcpToolsTests.cpp)
target_link_libraries(Editor.Scene.Tests PRIVATE Editor::Scene Editor::Core Foundation::Content Engine::Render Engine::Script Engine::Spline Engine::Animation Engine::Navigation Engine::Vegetation Foundation::Vegetation Pipeline::ModelImporter Foundation::VFS Foundation::Geometry Foundation::Materials Pipeline::Materials Foundation::Particles Foundation::Mcp Foundation::Json doctest::doctest)
target_compile_definitions(Editor.Scene.Tests PRIVATE DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS)
target_compile_options(Editor.Scene.Tests PRIVATE
    "$<$<CXX_COMPILER_ID:Clang>:-Wno-error=#warnings>" "$<$<CXX_COMPILER_ID:GNU>:-Wno-error=cpp>")
add_test(NAME Editor.Scene.Tests COMMAND Editor.Scene.Tests)
