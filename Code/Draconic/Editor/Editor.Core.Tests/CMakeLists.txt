add_executable(Draconic.Editor.Core.Tests TestsMain.cpp CommandTests.cpp ProjectTests.cpp
    ProjectRegistryTests.cpp
    ExportTests.cpp JobServiceTests.cpp ContextTests.cpp LogBufferTests.cpp ImporterTests.cpp)
target_link_libraries(Draconic.Editor.Core.Tests PRIVATE Draconic::Editor.Core Draconic::Geometry.Pipeline Draconic::Geometry.Resource
    Draconic::Engine.Render Draconic::Scene.Resource Draconic::VFS.Pak Draconic::Engine.Project Draconic::Content Draconic::VFS Draconic::Settings
    Draconic::Script.Wren Draconic::Script.Wren.Pipeline Draconic::Script.Pipeline Draconic::Script.Resource doctest::doctest)
target_compile_definitions(Draconic.Editor.Core.Tests PRIVATE DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS)
target_compile_options(Draconic.Editor.Core.Tests PRIVATE
    "$<$<CXX_COMPILER_ID:Clang>:-Wno-error=#warnings>" "$<$<CXX_COMPILER_ID:GNU>:-Wno-error=cpp>")
add_test(NAME Draconic.Editor.Core.Tests COMMAND Draconic.Editor.Core.Tests)
