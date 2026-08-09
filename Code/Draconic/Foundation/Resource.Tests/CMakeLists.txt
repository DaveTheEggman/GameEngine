# Foundation::Resource unit tests (doctest).

add_executable(Resource.Tests TestsMain.cpp ResourceTests.cpp AsyncResourceTests.cpp)
target_link_libraries(Resource.Tests PRIVATE Foundation::Resource Foundation::VFS doctest::doctest)
target_compile_definitions(Resource.Tests PRIVATE DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS)
target_compile_options(Resource.Tests PRIVATE
    "$<$<CXX_COMPILER_ID:Clang>:-Wno-error=#warnings;-Wno-error=c2y-extensions>" "$<$<CXX_COMPILER_ID:GNU>:-Wno-error=cpp>")
add_test(NAME Resource.Tests COMMAND Resource.Tests)
