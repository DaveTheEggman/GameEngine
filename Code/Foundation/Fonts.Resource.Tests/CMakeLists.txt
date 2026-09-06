add_executable(Fonts.Resource.Tests TestsMain.cpp FontResourceTests.cpp)
target_link_libraries(Fonts.Resource.Tests PRIVATE
    Foundation::Fonts.Resource Foundation::Fonts Foundation::Fonts.Coverage Foundation::Fonts.DistanceField
    Foundation::Image Foundation::Content Foundation::Resource Foundation::VFS doctest::doctest)
target_compile_definitions(Fonts.Resource.Tests PRIVATE DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS)
target_compile_options(Fonts.Resource.Tests PRIVATE
    "$<$<CXX_COMPILER_ID:Clang>:-Wno-error=#warnings>" "$<$<CXX_COMPILER_ID:GNU>:-Wno-error=cpp>")
add_test(NAME Fonts.Resource.Tests COMMAND Fonts.Resource.Tests)
