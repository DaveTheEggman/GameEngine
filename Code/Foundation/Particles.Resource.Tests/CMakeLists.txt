add_executable(Particles.Resource.Tests TestsMain.cpp ParticleEffectFactoryTests.cpp)
target_link_libraries(Particles.Resource.Tests PRIVATE
    Foundation::Particles.Resource Foundation::Particles
    Foundation::Content Foundation::Resource Foundation::VFS doctest::doctest)
target_compile_definitions(Particles.Resource.Tests PRIVATE DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS)
target_compile_options(Particles.Resource.Tests PRIVATE
    "$<$<CXX_COMPILER_ID:Clang>:-Wno-error=#warnings>" "$<$<CXX_COMPILER_ID:GNU>:-Wno-error=cpp>")
add_test(NAME Particles.Resource.Tests COMMAND Particles.Resource.Tests)
