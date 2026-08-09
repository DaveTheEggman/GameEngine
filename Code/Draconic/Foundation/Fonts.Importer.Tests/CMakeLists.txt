# Foundation::Fonts.Importer unit tests (doctest). Uses the bundled Roboto asset.

add_executable(Fonts.Importer.Tests TestsMain.cpp FontImporterTests.cpp)
target_link_libraries(Fonts.Importer.Tests PRIVATE Foundation::Fonts.Importer Foundation::Fonts.Baked Foundation::Fonts doctest::doctest)
target_compile_definitions(Fonts.Importer.Tests PRIVATE
    DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS
    DRACONIC_FONTS_ASSET_DIR="${CMAKE_SOURCE_DIR}/Data/Assets/fonts")
target_compile_options(Fonts.Importer.Tests PRIVATE
    "$<$<CXX_COMPILER_ID:Clang>:-Wno-error=#warnings;-Wno-error=c2y-extensions>" "$<$<CXX_COMPILER_ID:GNU>:-Wno-error=cpp>")
add_test(NAME Fonts.Importer.Tests COMMAND Fonts.Importer.Tests)
