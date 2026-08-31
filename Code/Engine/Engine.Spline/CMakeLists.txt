# Engine::Spline - `engine.spline`: the authorable 3D spline scene component (data-only; no
# subsystem, no tick). The curve math lives in Foundation::Spline; consumers and the editor
# tool evaluate/author through the component.

add_library(Engine.Spline STATIC)
add_library(Engine::Spline ALIAS Engine.Spline)

target_link_libraries(Engine.Spline PUBLIC
    Foundation::Core Foundation::Scene Foundation::Spline Foundation::Script.Facades Foundation::Policy)

target_sources(Engine.Spline
    PRIVATE
        SplineComponentsImpl.cpp
    PUBLIC
        FILE_SET engine_spline_modules TYPE CXX_MODULES FILES
            SplineComponents.cppm)
