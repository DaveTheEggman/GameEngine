add_executable(Draconic.Particles.Editor.Tests TestsMain.cpp ParticlePipelineTests.cpp)
target_link_libraries(Draconic.Particles.Editor.Tests PRIVATE
    Draconic::Particles.Editor Draconic::Particles.Resource Draconic::Particles
    Draconic::Content Draconic::Resource Draconic::Editor.Asset Draconic::VFS
    Draconic::Texture Draconic::Texture.Resource Draconic::RHI Draconic::RHI.Null
    doctest::doctest)
target_compile_definitions(Draconic.Particles.Editor.Tests PRIVATE DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS)
target_compile_options(Draconic.Particles.Editor.Tests PRIVATE
    "$<$<CXX_COMPILER_ID:Clang>:-Wno-error=#warnings>" "$<$<CXX_COMPILER_ID:GNU>:-Wno-error=cpp>")
add_test(NAME Draconic.Particles.Editor.Tests COMMAND Draconic.Particles.Editor.Tests)
