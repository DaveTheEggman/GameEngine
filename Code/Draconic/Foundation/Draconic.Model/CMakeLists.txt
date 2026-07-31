add_library(Draconic.Model STATIC)
add_library(Draconic::Model ALIAS Draconic.Model)
target_link_libraries(Draconic.Model PUBLIC Draconic::Core Draconic::Policy)
target_sources(Draconic.Model PUBLIC FILE_SET model_modules TYPE CXX_MODULES FILES
    ModelModule.cppm VertexFormat.cppm ModelVertex.cppm ModelMeshPart.cppm ModelTexture.cppm
    ModelMaterial.cppm ModelBone.cppm ModelMesh.cppm ModelAnimation.cppm ModelSkin.cppm Model.cppm)


if(DRACONIC_BUILD_TESTS)
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../Draconic.Model.Tests ${CMAKE_CURRENT_BINARY_DIR}/Tests)
endif()
