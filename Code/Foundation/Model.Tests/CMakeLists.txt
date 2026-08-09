add_executable(Model.Tests TestsMain.cpp ModelTests.cpp)
target_link_libraries(Model.Tests PRIVATE Foundation::Model doctest::doctest)
target_compile_definitions(Model.Tests PRIVATE DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS)
target_compile_options(Model.Tests PRIVATE
    "$<$<CXX_COMPILER_ID:Clang>:-Wno-error=#warnings;-Wno-error=c2y-extensions>" "$<$<CXX_COMPILER_ID:GNU>:-Wno-error=cpp>")
add_test(NAME Model.Tests COMMAND Model.Tests)
