add_executable(RaptorSample021_RayTracing Main.cpp)
target_link_libraries(RaptorSample021_RayTracing PRIVATE Raptor::SamplesFramework Raptor::RHIVulkan Raptor::Core Raptor::Policy)
target_compile_options(RaptorSample021_RayTracing PRIVATE
    "$<$<CXX_COMPILER_ID:Clang,GNU>:-Wno-error=unused-parameter;-Wno-error=unused-variable;-Wno-error=missing-field-initializers;-Wno-error=misleading-indentation>")
raptor_copy_sdl3_dll(RaptorSample021_RayTracing)
