# Draconic::Engine.Particles unit tests (doctest; the scene/resource integration - component
# resource-ref round-trip). Split out of Foundation/Particles.Tests: these exercise the ENGINE
# particle component, so they belong in the Engine collection (Foundation must not link Engine).

add_executable(Draconic.Engine.Particles.Tests
    TestsMain.cpp
    ResourceRefTests.cpp)

target_link_libraries(Draconic.Engine.Particles.Tests PRIVATE
    Draconic::Engine.Particles Draconic::Particles Draconic::Particles.Resource
    Draconic::Scene Draconic::Scene.Resource Draconic::Resource Draconic::VFS Draconic::Content doctest::doctest)

target_compile_definitions(Draconic.Engine.Particles.Tests PRIVATE
    DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS)

target_compile_options(Draconic.Engine.Particles.Tests PRIVATE
    "$<$<CXX_COMPILER_ID:Clang>:-Wno-error=#warnings;-Wno-error=c2y-extensions>"
    "$<$<CXX_COMPILER_ID:GNU>:-Wno-error=cpp>")

add_test(NAME Draconic.Engine.Particles.Tests COMMAND Draconic.Engine.Particles.Tests)
