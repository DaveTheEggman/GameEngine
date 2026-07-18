add_executable(DraconicPhysicsTests
    TestsMain.cpp
    PhysicsTests.cpp)

target_link_libraries(DraconicPhysicsTests PRIVATE
    Draconic::Physics doctest::doctest)

target_compile_definitions(DraconicPhysicsTests PRIVATE
    DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS)

target_compile_options(DraconicPhysicsTests PRIVATE
    "$<$<CXX_COMPILER_ID:Clang>:-Wno-error=#warnings;-Wno-error=c2y-extensions>"
    "$<$<CXX_COMPILER_ID:GNU>:-Wno-error=cpp>")

add_test(NAME DraconicPhysicsTests COMMAND DraconicPhysicsTests)
