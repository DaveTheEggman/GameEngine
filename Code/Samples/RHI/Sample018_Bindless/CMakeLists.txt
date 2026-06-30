add_executable(DraconicSample018_Bindless Main.cpp)
target_link_libraries(DraconicSample018_Bindless PRIVATE Draconic::SamplesFramework Draconic::RHIVulkan Draconic::Core Draconic::Policy)
target_compile_options(DraconicSample018_Bindless PRIVATE
    "$<$<CXX_COMPILER_ID:Clang,GNU>:-Wno-error=unused-parameter;-Wno-error=unused-variable;-Wno-error=missing-field-initializers;-Wno-error=misleading-indentation>")
draconic_copy_sdl3_dll(DraconicSample018_Bindless)
