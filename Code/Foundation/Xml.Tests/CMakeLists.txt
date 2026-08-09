
add_executable(Xml.Tests TestsMain.cpp LexerTests.cpp NodeTests.cpp NamespaceTests.cpp ParserTests.cpp WriterTests.cpp)
target_link_libraries(Xml.Tests PRIVATE Foundation::Xml doctest::doctest)
target_compile_definitions(Xml.Tests PRIVATE DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS)
target_compile_options(Xml.Tests PRIVATE
    "$<$<CXX_COMPILER_ID:Clang>:-Wno-error=#warnings>" "$<$<CXX_COMPILER_ID:GNU>:-Wno-error=cpp>")
add_test(NAME Xml.Tests COMMAND Xml.Tests)
