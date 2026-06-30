# Draconic::ModelImporter unit tests (doctest; loads a real glTF + cooks into a content DB — no GPU).
if(NOT TARGET doctest::doctest)
    include(FetchContent)
    FetchContent_Declare(doctest
        GIT_REPOSITORY https://github.com/doctest/doctest.git
        GIT_TAG v2.4.11
        GIT_SHALLOW TRUE
        SYSTEM)
    set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
    FetchContent_MakeAvailable(doctest)
    unset(CMAKE_POLICY_VERSION_MINIMUM)
endif()

add_executable(DraconicModelImporterTests
    TestsMain.cpp
    ModelImporterTests.cpp)

target_link_libraries(DraconicModelImporterTests PRIVATE
    Draconic::ModelImporter Draconic::VFS Draconic::Content Draconic::Resource
    Draconic::Geometry Draconic::MeshResource Draconic::Model
    Draconic::Animation Draconic::AnimationResource doctest::doctest)

target_compile_definitions(DraconicModelImporterTests PRIVATE
    DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS
    DRACONIC_MI_TEST_DUCK="${CMAKE_SOURCE_DIR}/Data/Assets/models/Duck/glTF/Duck.gltf"
    DRACONIC_MI_TEST_FOX="${CMAKE_SOURCE_DIR}/Data/Assets/models/Fox/glTF/Fox.gltf")

target_compile_options(DraconicModelImporterTests PRIVATE
    "$<$<CXX_COMPILER_ID:Clang>:-Wno-error=#warnings;-Wno-error=c2y-extensions>"
    "$<$<CXX_COMPILER_ID:GNU>:-Wno-error=cpp>")

add_test(NAME DraconicModelImporterTests COMMAND DraconicModelImporterTests)
