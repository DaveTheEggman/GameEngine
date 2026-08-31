# Foundation::Spline - `foundation.spline`: authorable 3D spline math (cubic-Bezier-native
# points with Auto/Smooth/Broken handle modes + arc length + closest point) over Core. A
# LEAF under Core (the Foundation::Lod precedent): domain math core must not know about.
# RHI-free; the scene component, editor tool, and future consumers (path-follow, scatter,
# road extrusion, particle spline) all share this one curve.

add_library(Spline STATIC)
add_library(Foundation::Spline ALIAS Spline)

target_link_libraries(Spline PUBLIC Foundation::Core Foundation::Policy)

target_sources(Spline
    PRIVATE
        SplineImpl.cpp
    PUBLIC
        FILE_SET spline_modules TYPE CXX_MODULES FILES
            Spline.cppm)
