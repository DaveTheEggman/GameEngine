# input.pipeline tests: InputMapAsset reflection (Nested map + traversable InputMap tree).
add_executable(DraconicInputEditorTests TestsMain.cpp InputMapAssetTests.cpp)
target_link_libraries(DraconicInputEditorTests PRIVATE
    Pipeline::Input Foundation::Input Pipeline::Core doctest::doctest)
target_compile_definitions(DraconicInputEditorTests PRIVATE DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS)
target_compile_options(DraconicInputEditorTests PRIVATE
    "$<$<CXX_COMPILER_ID:Clang>:-Wno-error=#warnings;-Wno-error=c2y-extensions>"
    "$<$<CXX_COMPILER_ID:GNU>:-Wno-error=cpp>")
add_test(NAME DraconicInputEditorTests COMMAND DraconicInputEditorTests)
