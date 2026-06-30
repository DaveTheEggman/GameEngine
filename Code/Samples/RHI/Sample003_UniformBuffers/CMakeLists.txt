add_executable(DraconicSample003_UniformBuffers Main.cpp)
target_link_libraries(DraconicSample003_UniformBuffers PRIVATE Draconic::SamplesFramework Draconic::RHIVulkan Draconic::Core Draconic::Policy)
target_compile_options(DraconicSample003_UniformBuffers PRIVATE
    "$<$<CXX_COMPILER_ID:Clang,GNU>:-Wno-error=unused-parameter;-Wno-error=unused-variable;-Wno-error=missing-field-initializers;-Wno-error=misleading-indentation>")
draconic_copy_sdl3_dll(DraconicSample003_UniformBuffers)
