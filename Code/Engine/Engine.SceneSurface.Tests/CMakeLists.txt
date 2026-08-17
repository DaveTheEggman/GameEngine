add_executable(Engine.SceneSurface.Tests TestsMain.cpp SceneSurfaceTests.cpp)
target_link_libraries(Engine.SceneSurface.Tests PRIVATE
    Engine::SceneSurface Foundation::Scene doctest::doctest)
target_compile_definitions(Engine.SceneSurface.Tests PRIVATE DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS)
target_compile_options(Engine.SceneSurface.Tests PRIVATE
    "$<$<CXX_COMPILER_ID:Clang>:-Wno-error=#warnings>" "$<$<CXX_COMPILER_ID:GNU>:-Wno-error=cpp>")
add_test(NAME Engine.SceneSurface.Tests COMMAND Engine.SceneSurface.Tests)
