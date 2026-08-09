add_executable(DraconicSample027_3DTexture Main.cpp)
target_link_libraries(DraconicSample027_3DTexture PRIVATE Samples::Framework Foundation::RHI.Vulkan Foundation::Core Foundation::Policy)
target_compile_options(DraconicSample027_3DTexture PRIVATE
    "$<$<CXX_COMPILER_ID:Clang,GNU>:-Wno-error=unused-parameter;-Wno-error=unused-variable;-Wno-error=missing-field-initializers;-Wno-error=misleading-indentation>")
draconic_copy_sdl3_dll(DraconicSample027_3DTexture)
