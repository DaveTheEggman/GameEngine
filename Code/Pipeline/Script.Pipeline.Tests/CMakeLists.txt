# The tests REGISTER the Wren backend (an entry point's job) - the library under test
# stays backend-neutral and resolves through the registry (B3).
# ScriptPipelineTests drives the neutral cook through the Wren cook; compile it only when Wren is
# on (ScriptReflectionTests is backend-neutral).
set(_script_pipeline_wren_test "")
if(OPTION_ENABLE_WREN)
    set(_script_pipeline_wren_test ScriptPipelineTests.cpp)
endif()
add_executable(Script.Pipeline.Tests TestsMain.cpp ${_script_pipeline_wren_test} ScriptReflectionTests.cpp)
target_link_libraries(Script.Pipeline.Tests PRIVATE
    Pipeline::Script ${SCRIPTLIB_WREN_PIPELINE} Foundation::Script.Resource Foundation::Script
    ${SCRIPTLIB_WREN} Foundation::Script.Facades
    Pipeline::Core Editor::Core Foundation::Content Foundation::Resource Foundation::VFS
    doctest::doctest)
target_compile_definitions(Script.Pipeline.Tests PRIVATE DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS)
target_compile_options(Script.Pipeline.Tests PRIVATE
    "$<$<CXX_COMPILER_ID:Clang>:-Wno-error=#warnings;-Wno-error=c2y-extensions>"
    "$<$<CXX_COMPILER_ID:GNU>:-Wno-error=cpp>")
add_test(NAME Script.Pipeline.Tests COMMAND Script.Pipeline.Tests)
