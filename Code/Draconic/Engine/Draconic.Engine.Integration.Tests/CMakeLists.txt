# draconic.engine.integration tests: the physics-contact -> script bridge (kind mapping + install
# semantics). End-to-end contact->behavior delivery through the same bridge is in the script scene tests.
add_executable(DraconicIntegrationTests TestsMain.cpp ContactBridgeTests.cpp)
target_link_libraries(DraconicIntegrationTests PRIVATE
    Draconic::Engine.Integration Draconic::Physics Draconic::Engine.Script doctest::doctest)
target_compile_definitions(DraconicIntegrationTests PRIVATE DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS)
target_compile_options(DraconicIntegrationTests PRIVATE
    "$<$<CXX_COMPILER_ID:Clang>:-Wno-error=#warnings;-Wno-error=c2y-extensions>"
    "$<$<CXX_COMPILER_ID:GNU>:-Wno-error=cpp>")
add_test(NAME DraconicIntegrationTests COMMAND DraconicIntegrationTests)
