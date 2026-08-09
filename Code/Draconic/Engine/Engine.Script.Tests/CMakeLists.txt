# Headless behaviors battery: a real Wren VM (registered by the TEST - the library
# stays backend-neutral), a real Scene, NO device/window dependencies.
add_executable(DraconicScriptSceneTests TestsMain.cpp ScriptSceneTests.cpp)
target_link_libraries(DraconicScriptSceneTests PRIVATE
    Engine::Script Foundation::Script.Resource Foundation::Scene Foundation::Scene.Resource
    Foundation::Content Foundation::Resource Foundation::VFS
    Foundation::Physics Engine::Physics Engine::Integration
    Engine::Render Foundation::Audio Engine::Audio
    Foundation::Animation Engine::Animation
    Foundation::Particles Engine::Particles
    Foundation::Net Foundation::Net.Replication
    Foundation::UI Engine::UI
    Foundation::Script Foundation::Script.Wren Foundation::Script.AngelScript doctest::doctest)
target_compile_definitions(DraconicScriptSceneTests PRIVATE DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS)
target_compile_options(DraconicScriptSceneTests PRIVATE
    "$<$<CXX_COMPILER_ID:Clang>:-Wno-error=#warnings;-Wno-error=c2y-extensions>"
    "$<$<CXX_COMPILER_ID:GNU>:-Wno-error=cpp>")
add_test(NAME DraconicScriptSceneTests COMMAND DraconicScriptSceneTests)
