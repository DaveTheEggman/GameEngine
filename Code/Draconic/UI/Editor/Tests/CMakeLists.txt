
add_executable(DraconicUIPipelineTests TestsMain.cpp UIPipelineTests.cpp)
target_link_libraries(DraconicUIPipelineTests PRIVATE
    Draconic::UIEditor Draconic::UIResource Draconic::UI Draconic::Editor
    Draconic::Content Draconic::Resource Draconic::VFS doctest::doctest)
target_compile_definitions(DraconicUIPipelineTests PRIVATE DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS)
target_compile_options(DraconicUIPipelineTests PRIVATE
    "$<$<CXX_COMPILER_ID:Clang>:-Wno-error=#warnings>" "$<$<CXX_COMPILER_ID:GNU>:-Wno-error=cpp>")
add_test(NAME DraconicUIPipelineTests COMMAND DraconicUIPipelineTests)
