add_executable(DraconicParticlesResourceTests TestsMain.cpp ParticleEffectFactoryTests.cpp)
target_link_libraries(DraconicParticlesResourceTests PRIVATE
    Draconic::ParticlesResource Draconic::Particles
    Draconic::Content Draconic::Resource Draconic::VFS doctest::doctest)
target_compile_definitions(DraconicParticlesResourceTests PRIVATE DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS)
target_compile_options(DraconicParticlesResourceTests PRIVATE
    "$<$<CXX_COMPILER_ID:Clang>:-Wno-error=#warnings>" "$<$<CXX_COMPILER_ID:GNU>:-Wno-error=cpp>")
add_test(NAME DraconicParticlesResourceTests COMMAND DraconicParticlesResourceTests)
