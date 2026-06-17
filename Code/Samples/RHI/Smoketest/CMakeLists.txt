add_executable(RaptorSmoketest Main.cpp)
target_link_libraries(RaptorSmoketest PRIVATE
    Raptor::RHIVulkan Raptor::RHINull Raptor::RHIValidation Raptor::Shaders
    Raptor::RuntimePlatformDesktop Raptor::Core Raptor::Policy)
target_compile_definitions(RaptorSmoketest PRIVATE RAPTOR_HAS_SHADERS)

# DX12 backend — Windows only.
if(TARGET Raptor::RHIDX12)
    target_link_libraries(RaptorSmoketest PRIVATE Raptor::RHIDX12)
    target_compile_definitions(RaptorSmoketest PRIVATE RAPTOR_HAS_DX12)
endif()

target_compile_options(RaptorSmoketest PRIVATE
    "$<$<CXX_COMPILER_ID:Clang,GNU>:-Wno-error=unused-parameter;-Wno-error=unused-variable;-Wno-error=missing-field-initializers;-Wno-error=misleading-indentation>")

# Copy SDL3.dll next to the executable on Windows.
if(RAPTOR_SDL3_DLL)
    add_custom_command(TARGET RaptorSmoketest POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${RAPTOR_SDL3_DLL}" "$<TARGET_FILE_DIR:RaptorSmoketest>")
endif()
