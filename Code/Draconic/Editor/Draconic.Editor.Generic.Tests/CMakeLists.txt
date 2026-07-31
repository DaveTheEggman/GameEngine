add_executable(Draconic.Editor.Generic.Tests TestsMain.cpp AssetFormTests.cpp)
target_link_libraries(Draconic.Editor.Generic.Tests PRIVATE
    Draconic::EditorGeneric Draconic::EditorCore Draconic::Content doctest::doctest)
target_compile_definitions(Draconic.Editor.Generic.Tests PRIVATE DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS)
target_compile_options(Draconic.Editor.Generic.Tests PRIVATE
    "$<$<CXX_COMPILER_ID:Clang>:-Wno-error=#warnings>" "$<$<CXX_COMPILER_ID:GNU>:-Wno-error=cpp>")
add_test(NAME Draconic.Editor.Generic.Tests COMMAND Draconic.Editor.Generic.Tests)
