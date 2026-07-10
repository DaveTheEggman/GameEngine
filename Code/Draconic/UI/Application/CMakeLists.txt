# Draconic::UIApplication - `draconic.ui.application`: the docking / workbench layer.
# RuntimeDockableWindowHost implements the toolkit IDockableWindowHost on the runtime multi-window host
# (IApplicationHost + UIHost + shell geometry). The ONLY UI module that links draconic.ui.toolkit, so
# docking cannot bleed into games or the toolkit-free ui.shell / ui.runtime layers.
add_library(DraconicUIApplication STATIC)
add_library(Draconic::UIApplication ALIAS DraconicUIApplication)

target_link_libraries(DraconicUIApplication
    PUBLIC
        Draconic::Core Draconic::Shell Draconic::Graphics Draconic::RuntimeClient
        Draconic::UI Draconic::UIToolkit Draconic::UIRuntime Draconic::Policy)

target_sources(DraconicUIApplication
    PUBLIC
        FILE_SET uiapplication_modules TYPE CXX_MODULES FILES
            UIApplication.cppm)
