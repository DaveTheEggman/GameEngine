add_executable(Image.Tests TestsMain.cpp ImageTests.cpp ImageDataTests.cpp ImageManipulationTests.cpp NormalMapTests.cpp)
target_link_libraries(Image.Tests PRIVATE Foundation::Image.IO Foundation::Image doctest::doctest)
target_compile_definitions(Image.Tests PRIVATE DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS)
target_compile_options(Image.Tests PRIVATE
    "$<$<CXX_COMPILER_ID:Clang>:-Wno-error=#warnings;-Wno-error=c2y-extensions>" "$<$<CXX_COMPILER_ID:GNU>:-Wno-error=cpp>")
add_test(NAME Image.Tests COMMAND Image.Tests)
