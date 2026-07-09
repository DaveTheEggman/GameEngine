# Draconic::UI unit tests (doctest) - faithful ports of Sedulous.UI.Tests, added as each subsystem lands.
add_executable(DraconicUITests
    TestsMain.cpp
    ThicknessTests.cpp
    ViewTransformTests.cpp
    ViewIdTests.cpp
    PropertyTests.cpp
    DrawableTests.cpp
    UnitTests.cpp
    SizeSpecTests.cpp
    BoxConstraintsTests.cpp
    GravityTests.cpp
    GravityHelperTests.cpp
    StyleSheetTests.cpp
    ThemeAtlasTests.cpp
    TokenizerTests.cpp
    SSSParserTests.cpp
    TestHelpers.cpp
    ViewTests.cpp
    ViewGroupTests.cpp
    UIContextTests.cpp
    LayoutTests.cpp
    InputTests.cpp
    EditingTests.cpp
    EditTextTests.cpp
    FontFamilyTests.cpp
    NumericFieldTests.cpp
    EditableLabelTests.cpp
    ScrollViewTests.cpp
    ControlTests.cpp)

target_link_libraries(DraconicUITests PRIVATE
    Draconic::UI doctest::doctest)

target_compile_definitions(DraconicUITests PRIVATE
    DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS)

target_compile_options(DraconicUITests PRIVATE
    "$<$<CXX_COMPILER_ID:Clang>:-Wno-error=#warnings;-Wno-error=c2y-extensions>"
    "$<$<CXX_COMPILER_ID:GNU>:-Wno-error=cpp>")

add_test(NAME DraconicUITests COMMAND DraconicUITests)
