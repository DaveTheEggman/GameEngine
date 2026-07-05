# Draconic::SceneResource unit tests (doctest; in-memory serialize round-trip).

add_executable(DraconicSceneResourceTests
    TestsMain.cpp
    SceneSerializeTests.cpp)

target_link_libraries(DraconicSceneResourceTests PRIVATE
    Draconic::SceneResource doctest::doctest)

target_compile_definitions(DraconicSceneResourceTests PRIVATE
    DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS)

target_compile_options(DraconicSceneResourceTests PRIVATE
    "$<$<CXX_COMPILER_ID:Clang>:-Wno-error=#warnings;-Wno-error=c2y-extensions>"
    "$<$<CXX_COMPILER_ID:GNU>:-Wno-error=cpp>")

add_test(NAME DraconicSceneResourceTests COMMAND DraconicSceneResourceTests)
