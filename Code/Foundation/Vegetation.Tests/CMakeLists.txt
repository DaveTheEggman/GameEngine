# Foundation::Vegetation unit tests (doctest; pure scatter math over an in-memory heightfield).

add_executable(Vegetation.Tests TestsMain.cpp ScatterTests.cpp)
target_link_libraries(Vegetation.Tests PRIVATE Foundation::Vegetation Foundation::Heightfield
    Foundation::Terrain Foundation::Terrain.Resource doctest::doctest)
target_compile_definitions(Vegetation.Tests PRIVATE DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS)
target_compile_options(Vegetation.Tests PRIVATE
    "$<$<CXX_COMPILER_ID:Clang>:-Wno-error=#warnings>" "$<$<CXX_COMPILER_ID:GNU>:-Wno-error=cpp>")
add_test(NAME Vegetation.Tests COMMAND Vegetation.Tests)
