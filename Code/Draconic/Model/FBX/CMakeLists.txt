add_library(DraconicModelFbx STATIC)
add_library(Draconic::ModelFbx ALIAS DraconicModelFbx)
target_link_libraries(DraconicModelFbx PUBLIC Draconic::Core Draconic::Model Draconic::ModelIO Draconic::Image Draconic::ImageIO Draconic::Policy PRIVATE Draconic::Ufbx)
target_sources(DraconicModelFbx PUBLIC FILE_SET model_fbx_modules TYPE CXX_MODULES FILES FbxLoader.cppm PRIVATE ufbx_impl.cpp)
target_compile_definitions(DraconicModelFbx PRIVATE _CRT_SECURE_NO_WARNINGS)
target_compile_options(DraconicModelFbx PRIVATE
    "$<$<CXX_COMPILER_ID:Clang,GNU>:-Wno-error=missing-field-initializers;-Wno-error=unused-function;-Wno-error=unused-but-set-variable;-Wno-error=sign-compare;-Wno-error=implicit-fallthrough;-Wno-error=type-limits;-Wno-error=cast-function-type>"
    "$<$<CXX_COMPILER_ID:Clang>:-Wno-gnu-anonymous-struct;-Wno-nested-anon-types;-Wno-language-extension-token>")
