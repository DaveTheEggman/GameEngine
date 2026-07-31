# Headless behaviors battery: a real Wren VM (registered by the TEST - the library
# stays backend-neutral), a real Scene, NO device/window dependencies.
add_executable(DraconicScriptSceneTests TestsMain.cpp ScriptSceneTests.cpp)
target_link_libraries(DraconicScriptSceneTests PRIVATE
    Draconic::Engine.Script Draconic::Script.Resource Draconic::Scene Draconic::Scene.Resource
    Draconic::Content Draconic::Resource Draconic::VFS
    Draconic::Physics Draconic::Engine.Physics
    Draconic::Script Draconic::Script.Wren Draconic::Script.AngelScript doctest::doctest)
target_compile_definitions(DraconicScriptSceneTests PRIVATE DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS)
target_compile_options(DraconicScriptSceneTests PRIVATE
    "$<$<CXX_COMPILER_ID:Clang>:-Wno-error=#warnings;-Wno-error=c2y-extensions>"
    "$<$<CXX_COMPILER_ID:GNU>:-Wno-error=cpp>")
add_test(NAME DraconicScriptSceneTests COMMAND DraconicScriptSceneTests)
