add_library(Model STATIC)
add_library(Foundation::Model ALIAS Model)
target_link_libraries(Model PUBLIC Foundation::Core Foundation::Policy)
target_sources(Model PUBLIC FILE_SET model_modules TYPE CXX_MODULES FILES
    ModelModule.cppm VertexFormat.cppm ModelVertex.cppm ModelMeshPart.cppm ModelTexture.cppm
    ModelMaterial.cppm ModelBone.cppm ModelMesh.cppm ModelAnimation.cppm ModelSkin.cppm Model.cppm)

