# Draconic::Particles unit tests (doctest; pure CPU, no GPU/renderer).

add_executable(DraconicParticlesTests
    TestsMain.cpp
    ParticlesTests.cpp)

target_link_libraries(DraconicParticlesTests PRIVATE
    Draconic::Particles doctest::doctest)

target_compile_definitions(DraconicParticlesTests PRIVATE
    DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS)

target_compile_options(DraconicParticlesTests PRIVATE
    "$<$<CXX_COMPILER_ID:Clang>:-Wno-error=#warnings;-Wno-error=c2y-extensions>"
    "$<$<CXX_COMPILER_ID:GNU>:-Wno-error=cpp>")

add_test(NAME DraconicParticlesTests COMMAND DraconicParticlesTests)
