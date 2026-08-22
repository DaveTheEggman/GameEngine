
add_executable(Engine.Net.Tests TestsMain.cpp SubsystemTests.cpp ReplicationLaneTests.cpp)
target_link_libraries(Engine.Net.Tests PRIVATE Engine::Net Foundation::Net doctest::doctest)
target_compile_definitions(Engine.Net.Tests PRIVATE DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS)
target_compile_options(Engine.Net.Tests PRIVATE
    "$<$<CXX_COMPILER_ID:Clang>:-Wno-error=#warnings>" "$<$<CXX_COMPILER_ID:GNU>:-Wno-error=cpp>")
add_test(NAME Engine.Net.Tests COMMAND Engine.Net.Tests)
