# Foundation::Fonts.Coverage.Baker unit tests (doctest). Uses the bundled Roboto asset.

add_executable(Fonts.Coverage.Baker.Tests TestsMain.cpp FontBakerTests.cpp)
target_link_libraries(Fonts.Coverage.Baker.Tests PRIVATE Foundation::VFS Foundation::Fonts.Coverage.Baker Foundation::Fonts.Coverage Foundation::Fonts doctest::doctest)
target_compile_definitions(Fonts.Coverage.Baker.Tests PRIVATE
    DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS)
target_compile_options(Fonts.Coverage.Baker.Tests PRIVATE
    "$<$<CXX_COMPILER_ID:Clang>:-Wno-error=#warnings;-Wno-error=c2y-extensions>" "$<$<CXX_COMPILER_ID:GNU>:-Wno-error=cpp>")
add_test(NAME Fonts.Coverage.Baker.Tests COMMAND Fonts.Coverage.Baker.Tests)
