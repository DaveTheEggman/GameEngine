
# Backend-neutral: the UI script-facade acceptance test moved to Integration::ScriptFacades.
add_executable(Engine.UI.Tests TestsMain.cpp UISubsystemTests.cpp)
target_link_libraries(Engine.UI.Tests PRIVATE Foundation::VFS
    Engine::UI Foundation::UI.Resource Foundation::UI Foundation::Scene
    Engine::Scene Foundation::Runtime Foundation::Input Engine::Input
    Foundation::Resource Foundation::Render Engine::Render Foundation::Shell
    Foundation::Shell.Null Foundation::RHI Foundation::RHI.Null
    Foundation::Script
    doctest::doctest)
target_compile_definitions(Engine.UI.Tests PRIVATE DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS)
target_compile_options(Engine.UI.Tests PRIVATE
    "$<$<CXX_COMPILER_ID:Clang>:-Wno-error=#warnings>" "$<$<CXX_COMPILER_ID:GNU>:-Wno-error=cpp>")
add_test(NAME Engine.UI.Tests COMMAND Engine.UI.Tests)
