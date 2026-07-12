add_executable(DraconicUIToolkitTests
    StyleProbeTests.cpp
    StatusBarTests.cpp
    ToolbarTests.cpp
    MenuBarTests.cpp
    SplitViewTests.cpp
    BreadcrumbBarTests.cpp
    ColorPickerTests.cpp
    HDRColorPickerTests.cpp
    GradientEditorTests.cpp
    VectorFieldsTests.cpp
    BoolEditorTests.cpp
    ButtonEditorTests.cpp
    StringEditorTests.cpp
    IntEditorTests.cpp
    FloatEditorTests.cpp
    RangeEditorTests.cpp
    EnumEditorTests.cpp
    Float2EditorTests.cpp Float3EditorTests.cpp Float4EditorTests.cpp
    ColorEditorTests.cpp
    PropertyGridTests.cpp
    CurveCanvasTests.cpp
    DockingTests.cpp
    DockPersistenceTests.cpp
    NodeGraphTests.cpp
    DraggableTreeViewTests.cpp
    ToolkitThemeExtensionTests.cpp
    ToolkitTests.cpp
    TestsMain.cpp)

target_link_libraries(DraconicUIToolkitTests PRIVATE
    Draconic::UIToolkit Draconic::UI Draconic::Core doctest::doctest)

target_compile_definitions(DraconicUIToolkitTests PRIVATE
    DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS)

target_compile_options(DraconicUIToolkitTests PRIVATE
    "$<$<CXX_COMPILER_ID:Clang>:-Wno-error=#warnings;-Wno-error=c2y-extensions>"
    "$<$<CXX_COMPILER_ID:GNU>:-Wno-error=cpp>")

add_test(NAME DraconicUIToolkitTests COMMAND DraconicUIToolkitTests)
