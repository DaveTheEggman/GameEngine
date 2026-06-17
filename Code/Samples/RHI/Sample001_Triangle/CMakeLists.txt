add_executable(RaptorSample001_Triangle Main.cpp)
target_link_libraries(RaptorSample001_Triangle PRIVATE Raptor::SamplesFramework Raptor::RHIVulkan Raptor::Core Raptor::Policy)
target_compile_options(RaptorSample001_Triangle PRIVATE
    "$<$<CXX_COMPILER_ID:Clang,GNU>:-Wno-error=unused-parameter;-Wno-error=unused-variable;-Wno-error=missing-field-initializers;-Wno-error=misleading-indentation>")
raptor_copy_sdl3_dll(RaptorSample001_Triangle)
