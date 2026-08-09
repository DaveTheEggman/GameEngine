# Foundation::UI.Viewport - `draconic.ui.viewport`: ViewportView, a ui::View that hosts 3D-rendered
# content (offscreen color+depth RT, an OnRender callback, displayed via the UI's VGContext as an
# external texture; gated input through a shell::InputSurface). Layered on draconic.ui; toolkit-free.
add_library(UI.Viewport STATIC)
add_library(Foundation::UI.Viewport ALIAS UI.Viewport)

target_link_libraries(UI.Viewport
    PUBLIC Foundation::Core Foundation::RHI Foundation::Image Foundation::VG Foundation::VG.Renderer Foundation::UI Foundation::Shell)

target_sources(UI.Viewport
    PUBLIC
        FILE_SET uiviewport_modules TYPE CXX_MODULES FILES
            ViewportView.cppm)

