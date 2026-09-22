add_executable(Vegetation.Pipeline.Tests TestsMain.cpp VegetationMaskPipelineTests.cpp)
target_link_libraries(Vegetation.Pipeline.Tests PRIVATE
    Pipeline::Vegetation Foundation::Vegetation.Resource
    Pipeline::Core Pipeline::Importer Foundation::Content Foundation::Resource Foundation::VFS
    Foundation::Image Foundation::Image.IO doctest::doctest)
target_compile_definitions(Vegetation.Pipeline.Tests PRIVATE DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS)
target_compile_options(Vegetation.Pipeline.Tests PRIVATE
    "$<$<CXX_COMPILER_ID:Clang>:-Wno-error=#warnings>" "$<$<CXX_COMPILER_ID:GNU>:-Wno-error=cpp>")
add_test(NAME Vegetation.Pipeline.Tests COMMAND Vegetation.Pipeline.Tests)
