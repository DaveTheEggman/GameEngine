# Draconic::UI - `draconic.ui`, the retained-mode UI framework (faithful port of
# Sedulous.UI). Platform-agnostic core; VG/Fonts are added as those phases land, and
# the ui.shell bridge is a separate library. Built bottom-up (Core first).
add_library(DraconicUI STATIC)
add_library(Draconic::UI ALIAS DraconicUI)

target_link_libraries(DraconicUI
    PUBLIC Draconic::Core Draconic::VG Draconic::VGSVG Draconic::Fonts Draconic::Image Draconic::Policy)

target_sources(DraconicUI
    PUBLIC
        FILE_SET ui_modules TYPE CXX_MODULES FILES
            Core/Enums.cppm
            Core/Thickness.cppm
            Core/ViewTransform.cppm
            Core/ViewId.cppm
            Core/Event.cppm
            Core/IPropertyOwner.cppm
            Core/Property.cppm
            Core/ICommand.cppm
            Core/IClipboard.cppm
            Core/View.cppm
            Styling/StyleProperty.cppm
            Styling/StyleValue.cppm
            Styling/StyleSelector.cppm
            Styling/StyleRule.cppm
            Styling/StyleSheet.cppm
            Debug/UIDebugDrawSettings.cppm
            Drawing/ControlState.cppm
            Drawing/UIDrawContext.cppm
            Drawing/Drawable.cppm
            Drawing/ColorDrawable.cppm
            Drawing/RoundedRectDrawable.cppm
            Drawing/ShapeDrawable.cppm
            Drawing/GradientDrawable.cppm
            Drawing/InsetDrawable.cppm
            Drawing/LayerDrawable.cppm
            Drawing/StateListDrawable.cppm
            Drawing/ImageDrawable.cppm
            Drawing/NineSliceDrawable.cppm
            Drawing/AtlasImageDrawable.cppm
            Drawing/AtlasNineSliceDrawable.cppm
            Drawing/SVGDrawable.cppm
            Layout/Unit.cppm
            Layout/SizeSpec.cppm
            Layout/BoxConstraints.cppm
            Layout/Gravity.cppm
            Layout/GravityHelper.cppm
            Layout/LayoutParams.cppm
            UIModule.cppm)

if(DRACONIC_BUILD_TESTS)
    add_subdirectory(Tests)
endif()
