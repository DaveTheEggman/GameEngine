# Foundation::Messaging unit tests (doctest; pure logic, the native EventBus).

add_executable(Messaging.Tests
    TestsMain.cpp
    EventBusTests.cpp)

target_link_libraries(Messaging.Tests PRIVATE
    Foundation::Messaging doctest::doctest)

target_compile_definitions(Messaging.Tests PRIVATE
    DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS)

target_compile_options(Messaging.Tests PRIVATE
    "$<$<CXX_COMPILER_ID:Clang>:-Wno-error=#warnings;-Wno-error=c2y-extensions>"
    "$<$<CXX_COMPILER_ID:GNU>:-Wno-error=cpp>")

add_test(NAME Messaging.Tests COMMAND Messaging.Tests)
