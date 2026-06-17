add_executable(RaptorSample025_MultiDrawIndirect Main.cpp)
target_link_libraries(RaptorSample025_MultiDrawIndirect PRIVATE Raptor::SamplesFramework Raptor::RHIVulkan Raptor::Core Raptor::Policy)
target_compile_options(RaptorSample025_MultiDrawIndirect PRIVATE
    "$<$<CXX_COMPILER_ID:Clang,GNU>:-Wno-error=unused-parameter;-Wno-error=unused-variable;-Wno-error=missing-field-initializers;-Wno-error=misleading-indentation>")
