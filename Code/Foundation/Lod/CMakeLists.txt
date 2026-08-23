# Foundation::Lod - `foundation.lod`: level-of-detail selection math (projected-sphere
# coverage + threshold walk + hysteresis) over Core. A LEAF under Core, deliberately its
# own library (user ruling 2026-08-23): LOD is a DOMAIN concept core math must not know
# about - domain math that would otherwise sneak into core lands here instead. RHI-free,
# so both foundation.render (mesh chains) and foundation.terrain (chunk geo-mip) share
# the one formula from either side of the RHI ceiling.

add_library(Lod STATIC)
add_library(Foundation::Lod ALIAS Lod)

target_link_libraries(Lod PUBLIC Foundation::Core Foundation::Policy)

target_sources(Lod
    PUBLIC
        FILE_SET lod_modules TYPE CXX_MODULES FILES
            LodMath.cppm)
