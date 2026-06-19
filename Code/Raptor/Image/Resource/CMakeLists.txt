# Raptor::ImageResource — `raptor.image.resource` (runtime CPU-image resource).
add_library(RaptorImageResource STATIC)
add_library(Raptor::ImageResource ALIAS RaptorImageResource)
target_link_libraries(RaptorImageResource PUBLIC Raptor::Core Raptor::Image Raptor::Content Raptor::Resource Raptor::Policy)
target_sources(RaptorImageResource PUBLIC FILE_SET image_resource_modules TYPE CXX_MODULES FILES ImageResource.cppm)
