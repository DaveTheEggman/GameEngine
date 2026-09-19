# Foundation::Scene.Resource unit tests (doctest; in-memory serialize round-trip).

add_executable(Scene.Resource.Tests
    TestsMain.cpp
    SceneSerializeTests.cpp
    SceneSaveLoadTests.cpp
    PrefabSpawnSystemTests.cpp)

target_link_libraries(Scene.Resource.Tests PRIVATE
    Foundation::Scene.Resource Foundation::VFS doctest::doctest)

target_compile_definitions(Scene.Resource.Tests PRIVATE
    DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS)

target_compile_options(Scene.Resource.Tests PRIVATE
    "$<$<CXX_COMPILER_ID:Clang>:-Wno-error=#warnings;-Wno-error=c2y-extensions>"
    "$<$<CXX_COMPILER_ID:GNU>:-Wno-error=cpp>")

add_test(NAME Scene.Resource.Tests COMMAND Scene.Resource.Tests)
