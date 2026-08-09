add_executable(Physics.Tests
    TestsMain.cpp
    PhysicsTests.cpp)

target_link_libraries(Physics.Tests PRIVATE
    Foundation::Physics doctest::doctest)

target_compile_definitions(Physics.Tests PRIVATE
    DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS)

target_compile_options(Physics.Tests PRIVATE
    "$<$<CXX_COMPILER_ID:Clang>:-Wno-error=#warnings;-Wno-error=c2y-extensions>"
    "$<$<CXX_COMPILER_ID:GNU>:-Wno-error=cpp>")

add_test(NAME Physics.Tests COMMAND Physics.Tests)
