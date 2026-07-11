add_executable(DraconicEditorCookTests TestsMain.cpp CookDriverTests.cpp)
target_link_libraries(DraconicEditorCookTests PRIVATE Draconic::EditorCook doctest::doctest)
target_compile_definitions(DraconicEditorCookTests PRIVATE DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS)
target_compile_options(DraconicEditorCookTests PRIVATE
    "$<$<CXX_COMPILER_ID:Clang>:-Wno-error=#warnings>" "$<$<CXX_COMPILER_ID:GNU>:-Wno-error=cpp>")
add_test(NAME DraconicEditorCookTests COMMAND DraconicEditorCookTests)
