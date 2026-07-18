# Draconic::ModelResource - `draconic.model.resource` (cooked model manifest + runtime composite + factory).
add_library(DraconicModelResource STATIC)
add_library(Draconic::ModelResource ALIAS DraconicModelResource)
target_link_libraries(DraconicModelResource PUBLIC
    Draconic::Core Draconic::Geometry Draconic::MeshResource Draconic::Materials Draconic::MaterialResource
    Draconic::Texture Draconic::TextureResource Draconic::Animation Draconic::AnimationResource
    Draconic::Resource Draconic::Content Draconic::Policy)
target_sources(DraconicModelResource PUBLIC FILE_SET model_resource_modules TYPE CXX_MODULES FILES ModelResource.cppm)
