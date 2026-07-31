# Draconic::ModelResource - `draconic.model.resource` (cooked model manifest + runtime composite + factory).
add_library(Draconic.Model.Resource STATIC)
add_library(Draconic::ModelResource ALIAS Draconic.Model.Resource)
target_link_libraries(Draconic.Model.Resource PUBLIC
    Draconic::Core Draconic::Geometry Draconic::MeshResource Draconic::Materials Draconic::MaterialResource
    Draconic::Texture Draconic::TextureResource Draconic::Animation Draconic::AnimationResource
    Draconic::Resource Draconic::Content Draconic::Policy)
target_sources(Draconic.Model.Resource PUBLIC FILE_SET model_resource_modules TYPE CXX_MODULES FILES ModelResource.cppm)
