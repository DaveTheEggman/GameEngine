# Foundation::Image.Resource - `foundation.image.resource` (runtime CPU-image resource).
add_library(Image.Resource STATIC)
add_library(Foundation::Image.Resource ALIAS Image.Resource)
target_link_libraries(Image.Resource PUBLIC Foundation::Core Foundation::Image Foundation::Content Foundation::Resource Foundation::Policy)
target_sources(Image.Resource PUBLIC FILE_SET image_resource_modules TYPE CXX_MODULES FILES ImageResource.cppm)
