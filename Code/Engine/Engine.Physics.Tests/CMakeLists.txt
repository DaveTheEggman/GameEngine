add_executable(Engine.Physics.Tests
    TestsMain.cpp
    PhysicsSceneTests.cpp)

target_link_libraries(Engine.Physics.Tests PRIVATE
    Engine::Physics Foundation::Scene Engine::Scene Foundation::Scene.Resource
    Foundation::Script doctest::doctest)

target_compile_definitions(Engine.Physics.Tests PRIVATE
    DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS)

target_compile_options(Engine.Physics.Tests PRIVATE
    "$<$<CXX_COMPILER_ID:Clang>:-Wno-error=#warnings;-Wno-error=c2y-extensions>"
    "$<$<CXX_COMPILER_ID:GNU>:-Wno-error=cpp>")

add_test(NAME Engine.Physics.Tests COMMAND Engine.Physics.Tests)
