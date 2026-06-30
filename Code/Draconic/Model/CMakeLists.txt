add_library(DraconicModel STATIC)
add_library(Draconic::Model ALIAS DraconicModel)
target_link_libraries(DraconicModel PUBLIC Draconic::Core Draconic::Policy)
target_sources(DraconicModel PUBLIC FILE_SET model_modules TYPE CXX_MODULES FILES
    ModelModule.cppm VertexFormat.cppm ModelVertex.cppm ModelMeshPart.cppm ModelTexture.cppm
    ModelMaterial.cppm ModelBone.cppm ModelMesh.cppm ModelAnimation.cppm ModelSkin.cppm Model.cppm)
add_subdirectory(IO)

add_subdirectory(GLTF)
add_subdirectory(FBX)

if(DRACONIC_BUILD_TESTS)
    add_subdirectory(Tests)
endif()
