add_executable(Pipeline.Importer.Tests TestsMain.cpp ImporterTests.cpp)
target_link_libraries(Pipeline.Importer.Tests PRIVATE Pipeline::Importer doctest::doctest)
target_compile_definitions(Pipeline.Importer.Tests PRIVATE DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS)
target_compile_options(Pipeline.Importer.Tests PRIVATE
    "$<$<CXX_COMPILER_ID:Clang>:-Wno-error=#warnings>" "$<$<CXX_COMPILER_ID:GNU>:-Wno-error=cpp>")
add_test(NAME Pipeline.Importer.Tests COMMAND Pipeline.Importer.Tests)
