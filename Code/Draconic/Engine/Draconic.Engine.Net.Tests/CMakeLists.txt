
add_executable(Draconic.Engine.Net.Tests TestsMain.cpp SubsystemTests.cpp)
target_link_libraries(Draconic.Engine.Net.Tests PRIVATE Draconic::NetSubsystem doctest::doctest)
target_compile_definitions(Draconic.Engine.Net.Tests PRIVATE DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS)
target_compile_options(Draconic.Engine.Net.Tests PRIVATE
    "$<$<CXX_COMPILER_ID:Clang>:-Wno-error=#warnings>" "$<$<CXX_COMPILER_ID:GNU>:-Wno-error=cpp>")
add_test(NAME Draconic.Engine.Net.Tests COMMAND Draconic.Engine.Net.Tests)
