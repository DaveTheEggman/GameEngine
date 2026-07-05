
add_executable(DraconicXmlSerializationTests TestsMain.cpp XmlSerializerTests.cpp)
target_link_libraries(DraconicXmlSerializationTests PRIVATE Draconic::XmlSerialization Draconic::Xml doctest::doctest)
target_compile_definitions(DraconicXmlSerializationTests PRIVATE DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS)
target_compile_options(DraconicXmlSerializationTests PRIVATE
    "$<$<CXX_COMPILER_ID:Clang>:-Wno-error=#warnings>" "$<$<CXX_COMPILER_ID:GNU>:-Wno-error=cpp>")
add_test(NAME DraconicXmlSerializationTests COMMAND DraconicXmlSerializationTests)
