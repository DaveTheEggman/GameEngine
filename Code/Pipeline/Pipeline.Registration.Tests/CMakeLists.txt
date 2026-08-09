add_executable(Pipeline.Registration.Tests TestsMain.cpp RegistrationTests.cpp)
target_link_libraries(Pipeline.Registration.Tests PRIVATE Pipeline::Registration doctest::doctest)
target_compile_definitions(Pipeline.Registration.Tests PRIVATE DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS)
target_compile_options(Pipeline.Registration.Tests PRIVATE
    "$<$<CXX_COMPILER_ID:Clang>:-Wno-error=#warnings>" "$<$<CXX_COMPILER_ID:GNU>:-Wno-error=cpp>")
add_test(NAME Pipeline.Registration.Tests COMMAND Pipeline.Registration.Tests)
