# scene.pipeline tests: a model manifest's generated prefab and scene, and what an import
# generates by its options.
add_executable(Scene.Pipeline.Tests TestsMain.cpp ModelPrefabTests.cpp)
target_link_libraries(Scene.Pipeline.Tests PRIVATE
    Pipeline::Scene Pipeline::ModelImporter Pipeline::Importer Engine::Render Engine::Animation
    Foundation::VFS Foundation::Content doctest::doctest)
target_compile_definitions(Scene.Pipeline.Tests PRIVATE DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS)
target_compile_options(Scene.Pipeline.Tests PRIVATE
    "$<$<CXX_COMPILER_ID:Clang>:-Wno-error=#warnings;-Wno-error=c2y-extensions>"
    "$<$<CXX_COMPILER_ID:GNU>:-Wno-error=cpp>")
add_test(NAME Scene.Pipeline.Tests COMMAND Scene.Pipeline.Tests)
