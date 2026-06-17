add_executable(RaptorSample011_MRT Main.cpp)
target_link_libraries(RaptorSample011_MRT PRIVATE Raptor::SamplesFramework Raptor::RHIVulkan Raptor::Core Raptor::Policy)
target_compile_options(RaptorSample011_MRT PRIVATE
    "$<$<CXX_COMPILER_ID:Clang,GNU>:-Wno-error=unused-parameter;-Wno-error=unused-variable;-Wno-error=missing-field-initializers;-Wno-error=misleading-indentation>")
