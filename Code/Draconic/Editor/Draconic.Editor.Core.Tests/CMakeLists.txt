add_executable(Draconic.Editor.Core.Tests TestsMain.cpp CommandTests.cpp ProjectTests.cpp
    ExportTests.cpp JobServiceTests.cpp ContextTests.cpp LogBufferTests.cpp ImporterTests.cpp)
target_link_libraries(Draconic.Editor.Core.Tests PRIVATE Draconic::EditorCore Draconic::MeshEditor Draconic::MeshResource
    Draconic::RenderSubsystem Draconic::SceneResource Draconic::SceneEditor Draconic::VFSPak Draconic::Project Draconic::Content Draconic::VFS Draconic::Settings
    Draconic::ScriptWren Draconic::ScriptWrenEditor Draconic::ScriptEditor Draconic::ScriptResource doctest::doctest)
target_compile_definitions(Draconic.Editor.Core.Tests PRIVATE DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS)
target_compile_options(Draconic.Editor.Core.Tests PRIVATE
    "$<$<CXX_COMPILER_ID:Clang>:-Wno-error=#warnings>" "$<$<CXX_COMPILER_ID:GNU>:-Wno-error=cpp>")
add_test(NAME Draconic.Editor.Core.Tests COMMAND Draconic.Editor.Core.Tests)
