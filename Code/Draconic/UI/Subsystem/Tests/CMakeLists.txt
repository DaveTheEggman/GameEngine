
add_executable(DraconicUISubsystemTests TestsMain.cpp UISubsystemTests.cpp)
target_link_libraries(DraconicUISubsystemTests PRIVATE
    Draconic::UISubsystem Draconic::UIResource Draconic::UI Draconic::Scene
    Draconic::SceneSubsystem Draconic::Runtime Draconic::Input Draconic::InputSubsystem
    Draconic::Resource Draconic::Render Draconic::RenderSubsystem Draconic::Shell doctest::doctest)
target_compile_definitions(DraconicUISubsystemTests PRIVATE DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS)
target_compile_options(DraconicUISubsystemTests PRIVATE
    "$<$<CXX_COMPILER_ID:Clang>:-Wno-error=#warnings>" "$<$<CXX_COMPILER_ID:GNU>:-Wno-error=cpp>")
add_test(NAME DraconicUISubsystemTests COMMAND DraconicUISubsystemTests)
