
add_executable(Draconic.Engine.UI.Tests TestsMain.cpp UISubsystemTests.cpp)
target_link_libraries(Draconic.Engine.UI.Tests PRIVATE
    Draconic::UISubsystem Draconic::UIResource Draconic::UI Draconic::Scene
    Draconic::SceneSubsystem Draconic::Runtime Draconic::Input Draconic::InputSubsystem
    Draconic::Resource Draconic::Render Draconic::RenderSubsystem Draconic::Shell
    Draconic::ShellNull Draconic::RHI Draconic::RHINull doctest::doctest)
target_compile_definitions(Draconic.Engine.UI.Tests PRIVATE DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS)
target_compile_options(Draconic.Engine.UI.Tests PRIVATE
    "$<$<CXX_COMPILER_ID:Clang>:-Wno-error=#warnings>" "$<$<CXX_COMPILER_ID:GNU>:-Wno-error=cpp>")
add_test(NAME Draconic.Engine.UI.Tests COMMAND Draconic.Engine.UI.Tests)
