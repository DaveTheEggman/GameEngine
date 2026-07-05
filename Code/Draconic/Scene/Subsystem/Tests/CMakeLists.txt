# Draconic::SceneSubsystem unit tests (doctest; Context + scenes, no GPU).

add_executable(DraconicSceneSubsystemTests
    TestsMain.cpp
    SceneSubsystemTests.cpp)

target_link_libraries(DraconicSceneSubsystemTests PRIVATE
    Draconic::SceneSubsystem doctest::doctest)

target_compile_definitions(DraconicSceneSubsystemTests PRIVATE
    DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS)

target_compile_options(DraconicSceneSubsystemTests PRIVATE
    "$<$<CXX_COMPILER_ID:Clang>:-Wno-error=#warnings;-Wno-error=c2y-extensions>"
    "$<$<CXX_COMPILER_ID:GNU>:-Wno-error=cpp>")

add_test(NAME DraconicSceneSubsystemTests COMMAND DraconicSceneSubsystemTests)
