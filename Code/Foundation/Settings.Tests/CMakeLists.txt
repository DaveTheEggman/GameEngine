add_executable(Settings.Tests TestsMain.cpp SettingsTests.cpp)
target_link_libraries(Settings.Tests PRIVATE
    Foundation::Settings Foundation::Core Foundation::Xml.Serialization Foundation::Xml doctest::doctest)
target_compile_definitions(Settings.Tests PRIVATE DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS)
target_compile_options(Settings.Tests PRIVATE
    "$<$<CXX_COMPILER_ID:Clang>:-Wno-error=#warnings>" "$<$<CXX_COMPILER_ID:GNU>:-Wno-error=cpp>")
add_test(NAME Settings.Tests COMMAND Settings.Tests)
