# Draconic::MaterialEditor unit tests (doctest; content DB — no GPU/DXC).

add_executable(DraconicMaterialEditorTests
    TestsMain.cpp
    MaterialEditorTests.cpp)

target_link_libraries(DraconicMaterialEditorTests PRIVATE
    Draconic::MaterialEditor Draconic::VFS doctest::doctest)

target_compile_definitions(DraconicMaterialEditorTests PRIVATE
    DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS)

target_compile_options(DraconicMaterialEditorTests PRIVATE
    "$<$<CXX_COMPILER_ID:Clang>:-Wno-error=#warnings;-Wno-error=c2y-extensions>"
    "$<$<CXX_COMPILER_ID:GNU>:-Wno-error=cpp>")

add_test(NAME DraconicMaterialEditorTests COMMAND DraconicMaterialEditorTests)
