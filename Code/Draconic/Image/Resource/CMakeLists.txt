# Draconic::ImageResource - `draconic.image.resource` (runtime CPU-image resource).
add_library(DraconicImageResource STATIC)
add_library(Draconic::ImageResource ALIAS DraconicImageResource)
target_link_libraries(DraconicImageResource PUBLIC Draconic::Core Draconic::Image Draconic::Content Draconic::Resource Draconic::Policy)
target_sources(DraconicImageResource PUBLIC FILE_SET image_resource_modules TYPE CXX_MODULES FILES ImageResource.cppm)
