# Foundation::Image.IO - `foundation.image.io` (load/save via stb).
add_library(Image.IO STATIC)
add_library(Foundation::Image.IO ALIAS Image.IO)
target_link_libraries(Image.IO PUBLIC Foundation::Image Foundation::Core Foundation::Policy PRIVATE ThirdParty::Stb)
target_sources(Image.IO
    PUBLIC FILE_SET image_io_modules TYPE CXX_MODULES FILES ImageIOModule.cppm
    PRIVATE stb_impl.cpp)
target_compile_options(Image.IO PRIVATE
    "$<$<CXX_COMPILER_ID:Clang,GNU>:-Wno-error=missing-field-initializers;-Wno-error=unused-function;-Wno-error=unused-but-set-variable;-Wno-error=sign-compare;-Wno-error=implicit-fallthrough;-Wno-error=type-limits>")
