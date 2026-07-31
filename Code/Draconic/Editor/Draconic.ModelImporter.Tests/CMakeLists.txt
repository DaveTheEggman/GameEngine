# Draconic::ModelImporter unit tests (doctest; loads a real glTF + cooks into a content DB - no GPU).

add_executable(Draconic.ModelImporter.Tests
    TestsMain.cpp
    ModelImporterTests.cpp)

target_link_libraries(Draconic.ModelImporter.Tests PRIVATE
    Draconic::ModelImporter Draconic::VFS Draconic::Content Draconic::Resource
    Draconic::Geometry Draconic::Geometry.Resource Draconic::Model
    Draconic::Animation Draconic::Animation.Resource
    Draconic::Editor.Core Draconic::Editor.Cook
    Draconic::RHI Draconic::RHI.Null Draconic::Texture.Resource Draconic::Materials.Resource
    Draconic::Texture.Editor Draconic::Geometry.Editor Draconic::Materials.Editor
    Draconic::Animation.Editor doctest::doctest)

target_compile_definitions(Draconic.ModelImporter.Tests PRIVATE
    DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS
    DRACONIC_MI_TEST_DUCK="${CMAKE_SOURCE_DIR}/Data/Assets/models/Duck/glTF/Duck.gltf"
    DRACONIC_MI_TEST_FOX="${CMAKE_SOURCE_DIR}/Data/Assets/models/Fox/glTF/Fox.gltf"
    DRACONIC_MI_TEST_GLB="${CMAKE_SOURCE_DIR}/Data/Assets/models/KenneyPlatformerCharacters/GLB/character-oozi.glb")

target_compile_options(Draconic.ModelImporter.Tests PRIVATE
    "$<$<CXX_COMPILER_ID:Clang>:-Wno-error=#warnings;-Wno-error=c2y-extensions>"
    "$<$<CXX_COMPILER_ID:GNU>:-Wno-error=cpp>")

add_test(NAME Draconic.ModelImporter.Tests COMMAND Draconic.ModelImporter.Tests)
