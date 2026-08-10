add_executable(Engine.ScriptSurface.Tests TestsMain.cpp ScriptSurfaceTests.cpp)
target_link_libraries(Engine.ScriptSurface.Tests PRIVATE
    Engine::ScriptSurface Foundation::Script.Facades doctest::doctest)
target_compile_definitions(Engine.ScriptSurface.Tests PRIVATE DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS)
target_compile_options(Engine.ScriptSurface.Tests PRIVATE
    "$<$<CXX_COMPILER_ID:Clang>:-Wno-error=#warnings>" "$<$<CXX_COMPILER_ID:GNU>:-Wno-error=cpp>")
add_test(NAME Engine.ScriptSurface.Tests COMMAND Engine.ScriptSurface.Tests)
