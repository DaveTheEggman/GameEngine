# Raptor::ModelImporter unit tests (doctest; loads a real glTF + cooks into a content DB — no GPU).
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

add_executable(RaptorModelImporterTests
    TestsMain.cpp
    ModelImporterTests.cpp)

target_link_libraries(RaptorModelImporterTests PRIVATE
    Raptor::ModelImporter Raptor::VFS Raptor::Content Raptor::Resource
    Raptor::Geometry Raptor::MeshResource Raptor::Model doctest::doctest)

target_compile_definitions(RaptorModelImporterTests PRIVATE
    DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS
    RAPTOR_MI_TEST_DUCK="${CMAKE_SOURCE_DIR}/Data/Assets/models/Duck/glTF/Duck.gltf")

target_compile_options(RaptorModelImporterTests PRIVATE
    "$<$<CXX_COMPILER_ID:Clang>:-Wno-error=#warnings;-Wno-error=c2y-extensions>"
    "$<$<CXX_COMPILER_ID:GNU>:-Wno-error=cpp>")

add_test(NAME RaptorModelImporterTests COMMAND RaptorModelImporterTests)
