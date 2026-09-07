# Foundation::Model.GLTF unit tests (doctest). Minimal documents are written to the test
# scratch dir; no fixtures are needed.

add_executable(Model.GLTF.Tests TestsMain.cpp GltfLoaderTests.cpp)
target_link_libraries(Model.GLTF.Tests PRIVATE Foundation::Model.GLTF Foundation::Model doctest::doctest)
target_compile_definitions(Model.GLTF.Tests PRIVATE DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS)
target_compile_options(Model.GLTF.Tests PRIVATE
    "$<$<CXX_COMPILER_ID:Clang>:-Wno-error=#warnings;-Wno-error=c2y-extensions>" "$<$<CXX_COMPILER_ID:GNU>:-Wno-error=cpp>")
add_test(NAME Model.GLTF.Tests COMMAND Model.GLTF.Tests)
