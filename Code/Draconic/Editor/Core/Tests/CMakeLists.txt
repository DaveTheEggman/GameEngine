add_executable(DraconicEditorCoreTests TestsMain.cpp CommandTests.cpp ProjectTests.cpp
    ExportTests.cpp JobServiceTests.cpp ContextTests.cpp LogBufferTests.cpp ImporterTests.cpp)
target_link_libraries(DraconicEditorCoreTests PRIVATE Draconic::EditorCore Draconic::MeshEditor Draconic::MeshResource
    Draconic::RenderSubsystem Draconic::SceneResource Draconic::SceneEditor Draconic::VFSPak Draconic::Project Draconic::Content Draconic::VFS Draconic::Settings
    Draconic::ScriptWren Draconic::ScriptWrenEditor Draconic::ScriptEditor Draconic::ScriptResource doctest::doctest)
target_compile_definitions(DraconicEditorCoreTests PRIVATE DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS)
target_compile_options(DraconicEditorCoreTests PRIVATE
    "$<$<CXX_COMPILER_ID:Clang>:-Wno-error=#warnings>" "$<$<CXX_COMPILER_ID:GNU>:-Wno-error=cpp>")
add_test(NAME DraconicEditorCoreTests COMMAND DraconicEditorCoreTests)
