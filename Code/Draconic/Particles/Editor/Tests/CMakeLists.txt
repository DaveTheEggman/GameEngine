add_executable(DraconicParticlesEditorTests TestsMain.cpp ParticlePipelineTests.cpp)
target_link_libraries(DraconicParticlesEditorTests PRIVATE
    Draconic::ParticlesEditor Draconic::ParticlesResource Draconic::Particles
    Draconic::Content Draconic::Resource Draconic::Editor Draconic::VFS
    Draconic::Texture Draconic::TextureResource Draconic::RHI Draconic::RHINull
    doctest::doctest)
target_compile_definitions(DraconicParticlesEditorTests PRIVATE DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS)
target_compile_options(DraconicParticlesEditorTests PRIVATE
    "$<$<CXX_COMPILER_ID:Clang>:-Wno-error=#warnings>" "$<$<CXX_COMPILER_ID:GNU>:-Wno-error=cpp>")
add_test(NAME DraconicParticlesEditorTests COMMAND DraconicParticlesEditorTests)
