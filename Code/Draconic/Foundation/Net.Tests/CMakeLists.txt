
add_executable(Net.Tests TestsMain.cpp WireTests.cpp TransportTests.cpp DatagramTests.cpp ReliableTests.cpp SessionTests.cpp RpcTests.cpp UdpSocketTests.cpp TcpSocketTests.cpp)
target_link_libraries(Net.Tests PRIVATE Foundation::Net doctest::doctest)
target_compile_definitions(Net.Tests PRIVATE DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS)
target_compile_options(Net.Tests PRIVATE
    "$<$<CXX_COMPILER_ID:Clang>:-Wno-error=#warnings>" "$<$<CXX_COMPILER_ID:GNU>:-Wno-error=cpp>")
add_test(NAME Net.Tests COMMAND Net.Tests)
