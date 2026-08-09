# Foundation::Texture.Resource - `draconic.texture.resource` (runtime cooked record + GPU factory).
add_library(Texture.Resource STATIC)
add_library(Foundation::Texture.Resource ALIAS Texture.Resource)
target_link_libraries(Texture.Resource PUBLIC Foundation::Core Foundation::RHI Foundation::Texture Foundation::Content Foundation::Resource Foundation::Policy)
target_sources(Texture.Resource PUBLIC FILE_SET texture_resource_modules TYPE CXX_MODULES FILES TextureResource.cppm)

