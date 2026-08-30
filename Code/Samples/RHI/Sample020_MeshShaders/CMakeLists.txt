add_executable(Sample020_MeshShaders Main.cpp)
target_link_libraries(Sample020_MeshShaders PRIVATE Samples::Framework Foundation::Core Foundation::Policy)
target_compile_options(Sample020_MeshShaders PRIVATE
    "$<$<CXX_COMPILER_ID:Clang,GNU>:-Wno-error=unused-parameter;-Wno-error=unused-variable;-Wno-error=missing-field-initializers;-Wno-error=misleading-indentation>")
util_copy_sdl3_dll(Sample020_MeshShaders)
