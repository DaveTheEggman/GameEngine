add_library(RaptorModelGltf STATIC)
add_library(Raptor::ModelGltf ALIAS RaptorModelGltf)
target_link_libraries(RaptorModelGltf PUBLIC Raptor::Core Raptor::Model Raptor::ModelIO Raptor::Image Raptor::ImageIO Raptor::Policy PRIVATE Raptor::Cgltf)
target_sources(RaptorModelGltf PUBLIC FILE_SET model_gltf_modules TYPE CXX_MODULES FILES GltfLoader.cppm PRIVATE cgltf_impl.cpp)
target_compile_definitions(RaptorModelGltf PRIVATE _CRT_SECURE_NO_WARNINGS)
target_compile_options(RaptorModelGltf PRIVATE
    "$<$<CXX_COMPILER_ID:Clang,GNU>:-Wno-error=missing-field-initializers;-Wno-error=unused-function;-Wno-error=unused-but-set-variable;-Wno-error=sign-compare;-Wno-error=implicit-fallthrough;-Wno-error=type-limits;-Wno-error=cast-function-type>"
    "$<$<CXX_COMPILER_ID:Clang>:-Wno-language-extension-token>")
