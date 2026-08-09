# Pipeline::ModelImporter unit tests (doctest; loads a real glTF + cooks into a content DB - no GPU).

add_executable(ModelImporter.Tests
    TestsMain.cpp
    ModelImporterTests.cpp)

target_link_libraries(ModelImporter.Tests PRIVATE
    Pipeline::ModelImporter Foundation::VFS Foundation::Content Foundation::Resource
    Foundation::Geometry Foundation::Geometry.Resource Foundation::Model
    Foundation::Animation Foundation::Animation.Resource
    Editor::Core Pipeline::Cook
    Foundation::RHI Foundation::RHI.Null Foundation::Texture.Resource Foundation::Materials.Resource
    Pipeline::Texture Pipeline::Geometry Pipeline::Materials
    Pipeline::Animation doctest::doctest)

target_compile_definitions(ModelImporter.Tests PRIVATE
    DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS
    TEST_MI_DUCK="${CMAKE_SOURCE_DIR}/Data/Assets/models/Duck/glTF/Duck.gltf"
    TEST_MI_FOX="${CMAKE_SOURCE_DIR}/Data/Assets/models/Fox/glTF/Fox.gltf"
    TEST_MI_GLB="${CMAKE_SOURCE_DIR}/Data/Assets/models/KenneyPlatformerCharacters/GLB/character-oozi.glb")

target_compile_options(ModelImporter.Tests PRIVATE
    "$<$<CXX_COMPILER_ID:Clang>:-Wno-error=#warnings;-Wno-error=c2y-extensions>"
    "$<$<CXX_COMPILER_ID:GNU>:-Wno-error=cpp>")

add_test(NAME ModelImporter.Tests COMMAND ModelImporter.Tests)
