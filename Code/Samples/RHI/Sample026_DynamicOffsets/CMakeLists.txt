add_executable(RaptorSample026_DynamicOffsets Main.cpp)
target_link_libraries(RaptorSample026_DynamicOffsets PRIVATE Raptor::SamplesFramework Raptor::RHIVulkan Raptor::Core Raptor::Policy)
target_compile_options(RaptorSample026_DynamicOffsets PRIVATE
    "$<$<CXX_COMPILER_ID:Clang,GNU>:-Wno-error=unused-parameter;-Wno-error=unused-variable;-Wno-error=missing-field-initializers;-Wno-error=misleading-indentation>")
raptor_copy_sdl3_dll(RaptorSample026_DynamicOffsets)
