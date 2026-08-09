
add_executable(Xml.Serialization.Tests TestsMain.cpp XmlSerializerTests.cpp)
target_link_libraries(Xml.Serialization.Tests PRIVATE Foundation::Xml.Serialization Foundation::Xml doctest::doctest)
target_compile_definitions(Xml.Serialization.Tests PRIVATE DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS)
target_compile_options(Xml.Serialization.Tests PRIVATE
    "$<$<CXX_COMPILER_ID:Clang>:-Wno-error=#warnings>" "$<$<CXX_COMPILER_ID:GNU>:-Wno-error=cpp>")
add_test(NAME Xml.Serialization.Tests COMMAND Xml.Serialization.Tests)
