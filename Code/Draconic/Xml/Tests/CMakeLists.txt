
add_executable(DraconicXmlTests TestsMain.cpp LexerTests.cpp NodeTests.cpp NamespaceTests.cpp ParserTests.cpp WriterTests.cpp)
target_link_libraries(DraconicXmlTests PRIVATE Draconic::Xml doctest::doctest)
target_compile_definitions(DraconicXmlTests PRIVATE DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS)
target_compile_options(DraconicXmlTests PRIVATE
    "$<$<CXX_COMPILER_ID:Clang>:-Wno-error=#warnings>" "$<$<CXX_COMPILER_ID:GNU>:-Wno-error=cpp>")
add_test(NAME DraconicXmlTests COMMAND DraconicXmlTests)
