# Draconic::SceneEditor unit tests (doctest; content DB save/load round-trip).

add_executable(DraconicSceneEditorTests
    TestsMain.cpp
    SceneSaveLoadTests.cpp)

target_link_libraries(DraconicSceneEditorTests PRIVATE
    Draconic::SceneEditor Draconic::VFS doctest::doctest)

target_compile_definitions(DraconicSceneEditorTests PRIVATE
    DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS)

target_compile_options(DraconicSceneEditorTests PRIVATE
    "$<$<CXX_COMPILER_ID:Clang>:-Wno-error=#warnings;-Wno-error=c2y-extensions>"
    "$<$<CXX_COMPILER_ID:GNU>:-Wno-error=cpp>")

add_test(NAME DraconicSceneEditorTests COMMAND DraconicSceneEditorTests)
