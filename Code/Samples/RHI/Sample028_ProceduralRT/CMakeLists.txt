add_executable(RaptorSample028_ProceduralRT Main.cpp)
target_link_libraries(RaptorSample028_ProceduralRT PRIVATE Raptor::SamplesFramework Raptor::RHIVulkan Raptor::Core Raptor::Policy)
target_compile_options(RaptorSample028_ProceduralRT PRIVATE
    "$<$<CXX_COMPILER_ID:Clang,GNU>:-Wno-error=unused-parameter;-Wno-error=unused-variable;-Wno-error=missing-field-initializers;-Wno-error=misleading-indentation>")
raptor_copy_sdl3_dll(RaptorSample028_ProceduralRT)
