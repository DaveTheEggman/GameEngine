# Draconic::GUI - `draconic.gui`, a fresh eepp-derived retained-mode UI framework,
# progressively sliced onto the Draconic VG/Fonts stack (docs/design/gui-port.md).
# Platform- and renderer-agnostic core; VG/Fonts/Image/Xml are linked as those phases
# land, and the gui.shell input bridge is a separate library. Built bottom-up.
add_library(DraconicGUI STATIC)
add_library(Draconic::GUI ALIAS DraconicGUI)

target_link_libraries(DraconicGUI
    PUBLIC Draconic::Core Draconic::VG Draconic::Policy)

target_sources(DraconicGUI
    PUBLIC
        FILE_SET gui_modules TYPE CXX_MODULES FILES
            Core/Rect.cppm
            Core/Transform2D.cppm
            Drawing/ControlState.cppm
            Drawing/DrawContext.cppm
            Drawing/Drawable.cppm
            Drawing/RectangleDrawable.cppm
            Drawing/BorderDrawable.cppm
            Drawing/StateListDrawable.cppm
            GUIModule.cppm)

if(DRACONIC_BUILD_TESTS)
    add_subdirectory(Tests)
endif()
