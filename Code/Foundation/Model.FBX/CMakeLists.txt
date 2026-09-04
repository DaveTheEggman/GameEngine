util_add_engine_library(Model.FBX ALIAS Foundation::Model.FBX)
target_link_libraries(Model.FBX PUBLIC Foundation::Core Foundation::Model Foundation::Model.IO Foundation::Image Foundation::Image.IO Foundation::Policy PRIVATE ThirdParty::Ufbx)
target_sources(Model.FBX PUBLIC FILE_SET model_fbx_modules TYPE CXX_MODULES FILES FbxLoader.cppm PRIVATE ufbx_impl.cpp)
target_compile_definitions(Model.FBX PRIVATE _CRT_SECURE_NO_WARNINGS)
target_compile_options(Model.FBX PRIVATE
    "$<$<CXX_COMPILER_ID:Clang,GNU>:-Wno-error=missing-field-initializers;-Wno-error=unused-function;-Wno-error=unused-but-set-variable;-Wno-error=sign-compare;-Wno-error=implicit-fallthrough;-Wno-error=type-limits;-Wno-error=cast-function-type>"
    "$<$<CXX_COMPILER_ID:Clang>:-Wno-gnu-anonymous-struct;-Wno-nested-anon-types;-Wno-language-extension-token>")
