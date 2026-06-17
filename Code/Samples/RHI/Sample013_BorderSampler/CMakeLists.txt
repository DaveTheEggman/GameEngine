add_executable(RaptorSample013_BorderSampler Main.cpp)
target_link_libraries(RaptorSample013_BorderSampler PRIVATE Raptor::SamplesFramework Raptor::RHIVulkan Raptor::Core Raptor::Policy)
target_compile_options(RaptorSample013_BorderSampler PRIVATE
    "$<$<CXX_COMPILER_ID:Clang,GNU>:-Wno-error=unused-parameter;-Wno-error=unused-variable;-Wno-error=missing-field-initializers;-Wno-error=misleading-indentation>")
