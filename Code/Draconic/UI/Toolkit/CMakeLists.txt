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
            ToolkitModule.cppm)

if(DRACONIC_BUILD_TESTS)
    add_subdirectory(Tests)
endif()
