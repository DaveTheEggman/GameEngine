
add_executable(Render.Pipeline.Tests TestsMain.cpp RenderProfileAssetTests.cpp)
target_link_libraries(Render.Pipeline.Tests PRIVATE
    Pipeline::Render Pipeline::Core Engine::Render
    Foundation::Content Foundation::Resource Foundation::VFS doctest::doctest)
target_compile_definitions(Render.Pipeline.Tests PRIVATE DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS)
target_compile_options(Render.Pipeline.Tests PRIVATE
    "$<$<CXX_COMPILER_ID:Clang>:-Wno-error=#warnings>" "$<$<CXX_COMPILER_ID:GNU>:-Wno-error=cpp>")
add_test(NAME Render.Pipeline.Tests COMMAND Render.Pipeline.Tests)
