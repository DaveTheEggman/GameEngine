add_executable(RaptorSample023_CubeMap Main.cpp)
target_link_libraries(RaptorSample023_CubeMap PRIVATE Raptor::SamplesFramework Raptor::RHIVulkan Raptor::Core Raptor::Policy)
target_compile_options(RaptorSample023_CubeMap PRIVATE
    "$<$<CXX_COMPILER_ID:Clang,GNU>:-Wno-error=unused-parameter;-Wno-error=unused-variable;-Wno-error=missing-field-initializers;-Wno-error=misleading-indentation>")
