add_executable(Draconic.Particles.Pipeline.Tests TestsMain.cpp ParticlePipelineTests.cpp)
target_link_libraries(Draconic.Particles.Pipeline.Tests PRIVATE
    Draconic::Particles.Pipeline Draconic::Particles.Resource Draconic::Particles
    Draconic::Content Draconic::Resource Draconic::Pipeline.Core Draconic::VFS
    Draconic::Texture Draconic::Texture.Resource Draconic::RHI Draconic::RHI.Null
    doctest::doctest)
target_compile_definitions(Draconic.Particles.Pipeline.Tests PRIVATE DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS)
target_compile_options(Draconic.Particles.Pipeline.Tests PRIVATE
    "$<$<CXX_COMPILER_ID:Clang>:-Wno-error=#warnings>" "$<$<CXX_COMPILER_ID:GNU>:-Wno-error=cpp>")
add_test(NAME Draconic.Particles.Pipeline.Tests COMMAND Draconic.Particles.Pipeline.Tests)
