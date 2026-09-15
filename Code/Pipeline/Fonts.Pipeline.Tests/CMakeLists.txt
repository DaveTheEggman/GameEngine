add_executable(Fonts.Pipeline.Tests TestsMain.cpp FontPipelineTests.cpp FontReflectionTests.cpp)
target_link_libraries(Fonts.Pipeline.Tests PRIVATE
    Pipeline::Fonts Foundation::Fonts.Resource Foundation::Fonts Pipeline::Core
    Editor::Core Foundation::Content Foundation::Resource Foundation::VFS Foundation::Image Foundation::Xml.Serialization doctest::doctest)
target_compile_definitions(Fonts.Pipeline.Tests PRIVATE
    DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS)
target_compile_options(Fonts.Pipeline.Tests PRIVATE
    "$<$<CXX_COMPILER_ID:Clang>:-Wno-error=#warnings>" "$<$<CXX_COMPILER_ID:GNU>:-Wno-error=cpp>")
add_test(NAME Fonts.Pipeline.Tests COMMAND Fonts.Pipeline.Tests)
