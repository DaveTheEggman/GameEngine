add_executable(DraconicScriptResourceTests TestsMain.cpp ScriptResourceTests.cpp)
target_link_libraries(DraconicScriptResourceTests PRIVATE
    Draconic::ScriptResource Draconic::Content Draconic::Resource Draconic::VFS
    doctest::doctest)
target_compile_definitions(DraconicScriptResourceTests PRIVATE DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS)
target_compile_options(DraconicScriptResourceTests PRIVATE
    "$<$<CXX_COMPILER_ID:Clang>:-Wno-error=#warnings;-Wno-error=c2y-extensions>"
    "$<$<CXX_COMPILER_ID:GNU>:-Wno-error=cpp>")
add_test(NAME DraconicScriptResourceTests COMMAND DraconicScriptResourceTests)
