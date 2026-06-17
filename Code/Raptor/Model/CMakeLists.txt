add_library(RaptorModel STATIC)
add_library(Raptor::Model ALIAS RaptorModel)
target_link_libraries(RaptorModel PUBLIC Raptor::Core Raptor::Policy)
target_sources(RaptorModel PUBLIC FILE_SET model_modules TYPE CXX_MODULES FILES
    ModelModule.cppm VertexFormat.cppm ModelVertex.cppm ModelMeshPart.cppm ModelTexture.cppm
    ModelMaterial.cppm ModelBone.cppm ModelMesh.cppm ModelAnimation.cppm ModelSkin.cppm Model.cppm)
add_subdirectory(IO)

add_subdirectory(GLTF)
add_subdirectory(FBX)

if(RAPTOR_BUILD_TESTS)
    add_subdirectory(Tests)
endif()
