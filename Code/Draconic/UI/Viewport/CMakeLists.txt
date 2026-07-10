# Draconic::UIViewport - `draconic.ui.viewport`: ViewportView, a ui::View that hosts 3D-rendered
# content (offscreen color+depth RT, an OnRender callback, displayed via the UI's VGContext as an
# external texture; gated input through a shell::InputSurface). Layered on draconic.ui; toolkit-free.
add_library(DraconicUIViewport STATIC)
add_library(Draconic::UIViewport ALIAS DraconicUIViewport)

target_link_libraries(DraconicUIViewport
    PUBLIC Draconic::Core Draconic::RHI Draconic::Image Draconic::VG Draconic::VGRenderer Draconic::UI Draconic::Shell)

target_sources(DraconicUIViewport
    PUBLIC
        FILE_SET uiviewport_modules TYPE CXX_MODULES FILES
            ViewportView.cppm)

if(DRACONIC_BUILD_TESTS)
    add_subdirectory(Tests)
endif()
