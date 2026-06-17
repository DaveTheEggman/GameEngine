# Raptor::ImageIO — `raptor.image.io` (load/save via stb).
add_library(RaptorImageIO STATIC)
add_library(Raptor::ImageIO ALIAS RaptorImageIO)
target_link_libraries(RaptorImageIO PUBLIC Raptor::Image Raptor::Core Raptor::Policy PRIVATE Raptor::Stb)
target_sources(RaptorImageIO
    PUBLIC FILE_SET image_io_modules TYPE CXX_MODULES FILES ImageIOModule.cppm
    PRIVATE stb_impl.cpp)
target_compile_options(RaptorImageIO PRIVATE
    "$<$<CXX_COMPILER_ID:Clang,GNU>:-Wno-error=missing-field-initializers;-Wno-error=unused-function;-Wno-error=unused-but-set-variable;-Wno-error=sign-compare;-Wno-error=implicit-fallthrough;-Wno-error=type-limits>")
