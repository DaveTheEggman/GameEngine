# Engine::Particles unit tests (doctest; the scene/resource integration - component
# resource-ref round-trip). Split out of Foundation/Particles.Tests: these exercise the ENGINE
# particle component, so they belong in the Engine collection (Foundation must not link Engine).

add_executable(Engine.Particles.Tests
    TestsMain.cpp
    ResourceRefTests.cpp)

target_link_libraries(Engine.Particles.Tests PRIVATE
    Engine::Particles Foundation::Particles Foundation::Particles.Resource
    Foundation::Scene Foundation::Scene.Resource Foundation::Resource Foundation::VFS Foundation::Content doctest::doctest)

target_compile_definitions(Engine.Particles.Tests PRIVATE
    DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS)

target_compile_options(Engine.Particles.Tests PRIVATE
    "$<$<CXX_COMPILER_ID:Clang>:-Wno-error=#warnings;-Wno-error=c2y-extensions>"
    "$<$<CXX_COMPILER_ID:GNU>:-Wno-error=cpp>")

add_test(NAME Engine.Particles.Tests COMMAND Engine.Particles.Tests)
