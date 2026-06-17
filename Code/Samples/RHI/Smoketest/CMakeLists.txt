add_executable(RaptorSmoketest Main.cpp)
target_link_libraries(RaptorSmoketest PRIVATE
    Raptor::RHIVulkan Raptor::RHINull Raptor::RHIValidation Raptor::Shaders
    Raptor::RuntimePlatformDesktop Raptor::Core Raptor::Policy)
target_compile_definitions(RaptorSmoketest PRIVATE RAPTOR_HAS_SHADERS)
target_compile_options(RaptorSmoketest PRIVATE
    "$<$<CXX_COMPILER_ID:Clang,GNU>:-Wno-error=unused-parameter;-Wno-error=unused-variable;-Wno-error=missing-field-initializers;-Wno-error=misleading-indentation>")
