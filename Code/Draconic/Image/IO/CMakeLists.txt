# Draconic::ImageIO — `draconic.image.io` (load/save via stb).
add_library(DraconicImageIO STATIC)
add_library(Draconic::ImageIO ALIAS DraconicImageIO)
target_link_libraries(DraconicImageIO PUBLIC Draconic::Image Draconic::Core Draconic::Policy PRIVATE Draconic::Stb)
target_sources(DraconicImageIO
    PUBLIC FILE_SET image_io_modules TYPE CXX_MODULES FILES ImageIOModule.cppm
    PRIVATE stb_impl.cpp)
target_compile_options(DraconicImageIO PRIVATE
    "$<$<CXX_COMPILER_ID:Clang,GNU>:-Wno-error=missing-field-initializers;-Wno-error=unused-function;-Wno-error=unused-but-set-variable;-Wno-error=sign-compare;-Wno-error=implicit-fallthrough;-Wno-error=type-limits>")
