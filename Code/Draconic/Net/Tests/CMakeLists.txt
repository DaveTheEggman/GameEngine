
add_executable(DraconicNetTests TestsMain.cpp WireTests.cpp TransportTests.cpp DatagramTests.cpp ReliableTests.cpp UdpSocketTests.cpp TcpSocketTests.cpp)
target_link_libraries(DraconicNetTests PRIVATE Draconic::Net doctest::doctest)
target_compile_definitions(DraconicNetTests PRIVATE DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS)
target_compile_options(DraconicNetTests PRIVATE
    "$<$<CXX_COMPILER_ID:Clang>:-Wno-error=#warnings>" "$<$<CXX_COMPILER_ID:GNU>:-Wno-error=cpp>")
add_test(NAME DraconicNetTests COMMAND DraconicNetTests)
