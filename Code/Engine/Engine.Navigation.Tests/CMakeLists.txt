add_executable(Engine.Navigation.Tests TestsMain.cpp NavigationSceneTests.cpp)
target_link_libraries(Engine.Navigation.Tests PRIVATE
    Engine::Navigation Foundation::Navigation Foundation::Navigation.Resource
    Foundation::Scene Foundation::Resource Foundation::Content Foundation::VFS
    Foundation::Core doctest::doctest)
target_compile_definitions(Engine.Navigation.Tests PRIVATE DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS)
target_compile_options(Engine.Navigation.Tests PRIVATE
    "$<$<CXX_COMPILER_ID:Clang>:-Wno-error=#warnings>" "$<$<CXX_COMPILER_ID:GNU>:-Wno-error=cpp>")
add_test(NAME Engine.Navigation.Tests COMMAND Engine.Navigation.Tests)
