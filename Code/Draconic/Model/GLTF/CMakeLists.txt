add_library(DraconicModelGltf STATIC)
add_library(Draconic::ModelGltf ALIAS DraconicModelGltf)
target_link_libraries(DraconicModelGltf PUBLIC Draconic::Core Draconic::Model Draconic::ModelIO Draconic::Image Draconic::ImageIO Draconic::Policy PRIVATE Draconic::Cgltf)
target_sources(DraconicModelGltf PUBLIC FILE_SET model_gltf_modules TYPE CXX_MODULES FILES GltfLoader.cppm PRIVATE cgltf_impl.cpp)
target_compile_definitions(DraconicModelGltf PRIVATE _CRT_SECURE_NO_WARNINGS)
target_compile_options(DraconicModelGltf PRIVATE
    "$<$<CXX_COMPILER_ID:Clang,GNU>:-Wno-error=missing-field-initializers;-Wno-error=unused-function;-Wno-error=unused-but-set-variable;-Wno-error=sign-compare;-Wno-error=implicit-fallthrough;-Wno-error=type-limits;-Wno-error=cast-function-type>"
    "$<$<CXX_COMPILER_ID:Clang>:-Wno-language-extension-token>")
