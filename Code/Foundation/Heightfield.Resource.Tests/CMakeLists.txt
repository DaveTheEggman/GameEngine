# Foundation::Heightfield.Resource unit tests (doctest; cook round-trip through a content DB).

add_executable(Heightfield.Resource.Tests
    TestsMain.cpp
    HeightfieldResourceTests.cpp)

target_link_libraries(Heightfield.Resource.Tests PRIVATE
    Foundation::Heightfield.Resource
    Foundation::Heightfield
    Foundation::Content
    Foundation::VFS
    Foundation::Resource
    doctest::doctest)

target_compile_definitions(Heightfield.Resource.Tests PRIVATE
    DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS)

target_compile_options(Heightfield.Resource.Tests PRIVATE
    "$<$<CXX_COMPILER_ID:Clang>:-Wno-error=#warnings;-Wno-error=c2y-extensions>"
    "$<$<CXX_COMPILER_ID:GNU>:-Wno-error=cpp>")

add_test(NAME Heightfield.Resource.Tests COMMAND Heightfield.Resource.Tests)
