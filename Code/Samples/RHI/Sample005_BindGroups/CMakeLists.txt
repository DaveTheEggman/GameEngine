add_executable(RaptorSample005_BindGroups Main.cpp)
target_link_libraries(RaptorSample005_BindGroups PRIVATE Raptor::SamplesFramework Raptor::RHIVulkan Raptor::Core Raptor::Policy)
target_compile_options(RaptorSample005_BindGroups PRIVATE
    "$<$<CXX_COMPILER_ID:Clang,GNU>:-Wno-error=unused-parameter;-Wno-error=unused-variable;-Wno-error=missing-field-initializers;-Wno-error=misleading-indentation>")
raptor_copy_sdl3_dll(RaptorSample005_BindGroups)
