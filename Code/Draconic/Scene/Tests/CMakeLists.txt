# Draconic::Scene unit tests (doctest; pure logic, no GPU).

add_executable(DraconicSceneTests
    TestsMain.cpp
    EntityTests.cpp
    TransformTests.cpp
    ComponentTests.cpp
    SceneSystemTests.cpp
    SceneManagerTests.cpp)

target_link_libraries(DraconicSceneTests PRIVATE
    Draconic::Scene doctest::doctest)

target_compile_definitions(DraconicSceneTests PRIVATE
    DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS)

target_compile_options(DraconicSceneTests PRIVATE
    "$<$<CXX_COMPILER_ID:Clang>:-Wno-error=#warnings;-Wno-error=c2y-extensions>"
    "$<$<CXX_COMPILER_ID:GNU>:-Wno-error=cpp>")

add_test(NAME DraconicSceneTests COMMAND DraconicSceneTests)
