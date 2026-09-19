add_executable(Engine.DefaultApp.Tests TestsMain.cpp StandardFactoriesTests.cpp FacadeSurfaceTests.cpp NetworkSpawnTests.cpp)
target_link_libraries(Engine.DefaultApp.Tests PRIVATE Engine::DefaultApp Engine::ScriptSurface
    Foundation::Script.Facades
    Foundation::Fonts.Resource Foundation::Texture.Resource Foundation::Content
    Foundation::Heightfield Foundation::Terrain.Resource
    Foundation::VFS Foundation::Xml Foundation::Scene Foundation::Scene.Resource doctest::doctest)
target_compile_definitions(Engine.DefaultApp.Tests PRIVATE DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS)
target_compile_options(Engine.DefaultApp.Tests PRIVATE
    "$<$<CXX_COMPILER_ID:Clang>:-Wno-error=#warnings>" "$<$<CXX_COMPILER_ID:GNU>:-Wno-error=cpp>")
add_test(NAME Engine.DefaultApp.Tests COMMAND Engine.DefaultApp.Tests)
