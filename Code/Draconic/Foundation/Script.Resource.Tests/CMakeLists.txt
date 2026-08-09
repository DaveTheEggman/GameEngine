add_executable(Script.Resource.Tests TestsMain.cpp ScriptResourceTests.cpp)
target_link_libraries(Script.Resource.Tests PRIVATE
    Foundation::Script.Resource Foundation::Content Foundation::Resource Foundation::VFS
    doctest::doctest)
target_compile_definitions(Script.Resource.Tests PRIVATE DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS)
target_compile_options(Script.Resource.Tests PRIVATE
    "$<$<CXX_COMPILER_ID:Clang>:-Wno-error=#warnings;-Wno-error=c2y-extensions>"
    "$<$<CXX_COMPILER_ID:GNU>:-Wno-error=cpp>")
add_test(NAME Script.Resource.Tests COMMAND Script.Resource.Tests)
