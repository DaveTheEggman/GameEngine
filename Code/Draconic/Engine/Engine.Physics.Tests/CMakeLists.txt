add_executable(DraconicPhysicsSceneTests
    TestsMain.cpp
    PhysicsSceneTests.cpp)

target_link_libraries(DraconicPhysicsSceneTests PRIVATE
    Draconic::Engine.Physics Draconic::Scene Draconic::Engine.Scene Draconic::Scene.Resource
    Draconic::Script Draconic::Script.Wren doctest::doctest)

target_compile_definitions(DraconicPhysicsSceneTests PRIVATE
    DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS)

target_compile_options(DraconicPhysicsSceneTests PRIVATE
    "$<$<CXX_COMPILER_ID:Clang>:-Wno-error=#warnings;-Wno-error=c2y-extensions>"
    "$<$<CXX_COMPILER_ID:GNU>:-Wno-error=cpp>")

add_test(NAME DraconicPhysicsSceneTests COMMAND DraconicPhysicsSceneTests)
