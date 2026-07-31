# Draconic::Scene.Editor unit tests (doctest; content DB save/load round-trip).

add_executable(Draconic.Scene.Editor.Tests
    TestsMain.cpp
    SceneSaveLoadTests.cpp)

target_link_libraries(Draconic.Scene.Editor.Tests PRIVATE
    Draconic::Scene.Editor Draconic::VFS doctest::doctest)

target_compile_definitions(Draconic.Scene.Editor.Tests PRIVATE
    DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS)

target_compile_options(Draconic.Scene.Editor.Tests PRIVATE
    "$<$<CXX_COMPILER_ID:Clang>:-Wno-error=#warnings;-Wno-error=c2y-extensions>"
    "$<$<CXX_COMPILER_ID:GNU>:-Wno-error=cpp>")

add_test(NAME Draconic.Scene.Editor.Tests COMMAND Draconic.Scene.Editor.Tests)
