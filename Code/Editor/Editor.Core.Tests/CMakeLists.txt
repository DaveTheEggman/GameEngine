# ExportTests exercises the editor export flow with a cooked Wren script asset as its sample; it
# (and its Wren cook deps) compile only when Wren is on. The rest of the suite is backend-neutral.
set(_editor_core_script_test "")
set(_editor_core_script_libs "")
if(OPTION_ENABLE_WREN)
    set(_editor_core_script_test ExportTests.cpp)
    set(_editor_core_script_libs
        ${SCRIPTLIB_WREN} ${SCRIPTLIB_WREN_PIPELINE} Pipeline::Script Foundation::Script.Resource)
endif()
add_executable(Editor.Core.Tests TestsMain.cpp CommandTests.cpp ProjectTests.cpp
    ProjectRegistryTests.cpp
    ${_editor_core_script_test} JobServiceTests.cpp ContextTests.cpp LogBufferTests.cpp)
target_link_libraries(Editor.Core.Tests PRIVATE Editor::Core Pipeline::Geometry Foundation::Geometry.Resource
    Engine::Render Foundation::Scene.Resource Foundation::VFS.Pak Engine::Project Foundation::Content Foundation::VFS Foundation::Settings
    ${_editor_core_script_libs} doctest::doctest)
target_compile_definitions(Editor.Core.Tests PRIVATE DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS)
target_compile_options(Editor.Core.Tests PRIVATE
    "$<$<CXX_COMPILER_ID:Clang>:-Wno-error=#warnings>" "$<$<CXX_COMPILER_ID:GNU>:-Wno-error=cpp>")
add_test(NAME Editor.Core.Tests COMMAND Editor.Core.Tests)
