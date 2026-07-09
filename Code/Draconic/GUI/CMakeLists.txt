# Draconic::GUI - `draconic.gui`, a fresh eepp-derived retained-mode UI framework,
# progressively sliced onto the Draconic VG/Fonts stack (docs/design/gui-port.md).
# Platform- and renderer-agnostic core; VG/Fonts/Image/Xml are linked as those phases
# land, and the gui.shell input bridge is a separate library. Built bottom-up.
add_library(DraconicGUI STATIC)
add_library(Draconic::GUI ALIAS DraconicGUI)

target_link_libraries(DraconicGUI
    PUBLIC Draconic::Core Draconic::VG Draconic::Image Draconic::Fonts Draconic::Policy)

target_sources(DraconicGUI
    PUBLIC
        FILE_SET gui_modules TYPE CXX_MODULES FILES
            Core/Rect.cppm
            Core/Transform2D.cppm
            Core/Thickness.cppm
            Core/Transformable.cppm
            Core/Event.cppm
            Core/MutationQueue.cppm
            Core/Node.cppm
            Scene/Action.cppm
            Scene/ActionManager.cppm
            Scene/Actions.cppm
            Scene/EventDispatcher.cppm
            Scene/SceneNode.cppm
            Drawing/ControlState.cppm
            Drawing/DrawContext.cppm
            Drawing/Drawable.cppm
            Drawing/RectangleDrawable.cppm
            Drawing/BorderDrawable.cppm
            Drawing/StateListDrawable.cppm
            Drawing/LinearGradientDrawable.cppm
            Drawing/RadialGradientDrawable.cppm
            Drawing/ImageDrawable.cppm
            Drawing/NineSliceDrawable.cppm
            Drawing/LayerDrawable.cppm
            Text/Text.cppm
            Widgets/UINode.cppm
            Widgets/UIWidget.cppm
            Widgets/Label.cppm
            Widgets/Button.cppm
            Styling/ParseUtil.cppm
            Styling/StyleSelector.cppm
            Styling/MediaQuery.cppm
            Styling/StyleRule.cppm
            Styling/StyleSheet.cppm
            Styling/CSSParser.cppm
            Styling/CSSValues.cppm
            Styling/StyleApplier.cppm
            Styling/Transition.cppm
            Styling/StyleManager.cppm
            GUIModule.cppm
    PRIVATE
        # Module implementation unit: Node method bodies that call the EventDispatcher
        # (kept out of :node to avoid a :node <-> :event_dispatcher partition cycle).
        Scene/GUIClusterImpl.cpp)

if(DRACONIC_BUILD_TESTS)
    add_subdirectory(Tests)
endif()
