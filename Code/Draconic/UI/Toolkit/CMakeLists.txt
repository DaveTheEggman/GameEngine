# Draconic::UIToolkit - `draconic.ui.toolkit`: the editor-grade widget set (docking, property grid,
# node graph, color pickers, curve editor, menu/tool/status bars) layered on draconic.ui. Faithful port
# of Sedulous.UI.Toolkit. A separate library/module so the core draconic.ui stays lean; consumers write
# `import draconic.ui.toolkit;`.
add_library(DraconicUIToolkit STATIC)
add_library(Draconic::UIToolkit ALIAS DraconicUIToolkit)

target_link_libraries(DraconicUIToolkit
    PUBLIC Draconic::Core Draconic::VG Draconic::VGSVG Draconic::Fonts Draconic::Image Draconic::UI Draconic::Policy)

target_sources(DraconicUIToolkit
    PUBLIC
        FILE_SET uitoolkit_modules TYPE CXX_MODULES FILES
            StatusBar.cppm
            ToastHost.cppm
            Toolbar.cppm
            MenuBar.cppm
            SplitView.cppm
            BreadcrumbBar.cppm
            ColorPicker.cppm
            HDRColorPicker.cppm
            GradientEditor.cppm
            VectorFields.cppm
            Float2Editor.cppm
            Float4Editor.cppm
            PropertyEditor.cppm
            BoolEditor.cppm
            ButtonEditor.cppm
            StringEditor.cppm
            IntEditor.cppm
            FloatEditor.cppm
            RangeEditor.cppm
            EnumEditor.cppm
            Float3Editor.cppm
            ColorEditor.cppm
            PropertyGrid.cppm
            CurveCanvas.cppm
            DockPosition.cppm
            DockLayoutNode.cppm
            IDockHost.cppm
            IDockableWindowHost.cppm
            DockZoneIndicator.cppm
            DockSplit.cppm
            Docking.cppm
            NodeGraphTypes.cppm
            NodeGraphCanvas.cppm
            DraggableTreeView.cppm
            Code/CodeDocument.cppm
            Code/CodeLexer.cppm
            Code/CodeLexerScan.cppm
            Code/CodeEditView.cppm
            ToolkitThemeExtension.cppm
            ToolkitModule.cppm)

# Implementation units for the code-editor stack (declared in the Code/ partitions).
target_sources(DraconicUIToolkit PRIVATE
    Code/CodeHighlighter.cpp
    Code/CLikeLexer.cpp
    Code/XmlLexer.cpp
    Code/CodeLexerRegistry.cpp)

if(DRACONIC_BUILD_TESTS)
    add_subdirectory(Tests)
endif()
