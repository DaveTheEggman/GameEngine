add_executable(RaptorSample030_RenderBundles Main.cpp)
target_link_libraries(RaptorSample030_RenderBundles PRIVATE Raptor::SamplesFramework Raptor::RHIVulkan Raptor::Core Raptor::Policy)
target_compile_options(RaptorSample030_RenderBundles PRIVATE
    "$<$<CXX_COMPILER_ID:Clang,GNU>:-Wno-error=unused-parameter;-Wno-error=unused-variable;-Wno-error=missing-field-initializers;-Wno-error=misleading-indentation>")
raptor_copy_sdl3_dll(RaptorSample030_RenderBundles)
