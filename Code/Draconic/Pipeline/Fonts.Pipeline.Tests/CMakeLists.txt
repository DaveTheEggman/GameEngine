add_executable(Draconic.Fonts.Pipeline.Tests TestsMain.cpp FontPipelineTests.cpp FontReflectionTests.cpp)
target_link_libraries(Draconic.Fonts.Pipeline.Tests PRIVATE
    Draconic::Fonts.Pipeline Draconic::Fonts.Resource Draconic::Fonts Draconic::Pipeline.Core
    Draconic::Editor.Core Draconic::Content Draconic::Resource Draconic::VFS Draconic::Image doctest::doctest)
target_compile_definitions(Draconic.Fonts.Pipeline.Tests PRIVATE
    DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS
    DRACONIC_TEST_FONT_PATH="${CMAKE_SOURCE_DIR}/Data/Assets/fonts/dejavu/DejaVuSansMono.ttf")
target_compile_options(Draconic.Fonts.Pipeline.Tests PRIVATE
    "$<$<CXX_COMPILER_ID:Clang>:-Wno-error=#warnings>" "$<$<CXX_COMPILER_ID:GNU>:-Wno-error=cpp>")
add_test(NAME Draconic.Fonts.Pipeline.Tests COMMAND Draconic.Fonts.Pipeline.Tests)
