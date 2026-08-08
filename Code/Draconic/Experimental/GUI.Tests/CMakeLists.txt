# Draconic::GUI unit tests (doctest), added per subsystem as the port lands.
add_executable(Draconic.GUI.Tests
    TestsMain.cpp
    RectTests.cpp
    Transform2DTests.cpp
    DrawingTests.cpp
    GradientImageTests.cpp
    CompositorTests.cpp
    TransformableTests.cpp
    NodeTests.cpp
    NodeDrawTests.cpp
    ActionTests.cpp
    SceneNodeTests.cpp
    EventDispatcherTests.cpp
    TextTests.cpp
    ModelTests.cpp
    WidgetTests.cpp
    StyleSelectorTests.cpp
    StyleSheetTests.cpp
    CSSParserTests.cpp
    StyleApplierTests.cpp
    CssCascadeTests.cpp
    MediaQueryTests.cpp
    TransitionTests.cpp
    StyleManagerTests.cpp
    LabelButtonTests.cpp
    LinearLayoutTests.cpp
    FlexLayoutTests.cpp
    CheckBoxTests.cpp
    SliderTests.cpp
    ProgressBarTests.cpp
    TextFieldTests.cpp
    LayoutTests.cpp
    ScrollTests.cpp
    RadioTests.cpp
    ImageWidgetTests.cpp
    ListBoxTests.cpp
    ListViewTests.cpp
    TableViewTests.cpp
    SortingProxyModelTests.cpp
    TreeViewTests.cpp
    TabWidgetTests.cpp
    WindowTests.cpp
    MessageBoxTests.cpp
    ComboBoxTests.cpp
    MenuTests.cpp
    MenuBarTests.cpp
    TooltipTests.cpp
    DragDropTests.cpp
    ThemeTests.cpp
    KeyframesTests.cpp
    MarkupTests.cpp)

target_link_libraries(Draconic.GUI.Tests PRIVATE
    Draconic::GUI doctest::doctest)

target_compile_definitions(Draconic.GUI.Tests PRIVATE
    DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS)

target_compile_options(Draconic.GUI.Tests PRIVATE
    "$<$<CXX_COMPILER_ID:Clang>:-Wno-error=#warnings;-Wno-error=c2y-extensions>"
    "$<$<CXX_COMPILER_ID:GNU>:-Wno-error=cpp>")

add_test(NAME Draconic.GUI.Tests COMMAND Draconic.GUI.Tests)
