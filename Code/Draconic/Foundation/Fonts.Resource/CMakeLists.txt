# Foundation::Fonts.Resource - `draconic.fonts.resource` (the cooked font record + runtime
# Font product + FontFactory - the triad tier Sedulous.Fonts.Resources provided). Device-
# free: atlas images stay CPU-side; the VG layer uploads them like any font texture.

add_library(Fonts.Resource STATIC)
add_library(Foundation::Fonts.Resource ALIAS Fonts.Resource)

target_link_libraries(Fonts.Resource
    PUBLIC Foundation::Core Foundation::Fonts Foundation::Fonts.Baked Foundation::Fonts.TTF Foundation::Fonts.DistanceField
           Foundation::Image Foundation::Content Foundation::Resource Foundation::Policy)

target_sources(Fonts.Resource
    PUBLIC
        FILE_SET fonts_resource_modules TYPE CXX_MODULES FILES
            FontResource.cppm)
