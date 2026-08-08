# Draconic::Engine.Scene unit tests (doctest; Context + scenes, no GPU).

add_executable(Draconic.Engine.Scene.Tests
    TestsMain.cpp
    SceneSubsystemTests.cpp)

target_link_libraries(Draconic.Engine.Scene.Tests PRIVATE
    Draconic::Engine.Scene doctest::doctest)

target_compile_definitions(Draconic.Engine.Scene.Tests PRIVATE
    DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS)

target_compile_options(Draconic.Engine.Scene.Tests PRIVATE
    "$<$<CXX_COMPILER_ID:Clang>:-Wno-error=#warnings;-Wno-error=c2y-extensions>"
    "$<$<CXX_COMPILER_ID:GNU>:-Wno-error=cpp>")

add_test(NAME Draconic.Engine.Scene.Tests COMMAND Draconic.Engine.Scene.Tests)
