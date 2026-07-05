# Draconic::Geometry unit tests (doctest; pure data, no GPU).

add_executable(DraconicGeometryTests
    TestsMain.cpp
    GeometryTests.cpp)

target_link_libraries(DraconicGeometryTests PRIVATE
    Draconic::Geometry doctest::doctest)

target_compile_definitions(DraconicGeometryTests PRIVATE
    DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS)

target_compile_options(DraconicGeometryTests PRIVATE
    "$<$<CXX_COMPILER_ID:Clang>:-Wno-error=#warnings;-Wno-error=c2y-extensions>"
    "$<$<CXX_COMPILER_ID:GNU>:-Wno-error=cpp>")

add_test(NAME DraconicGeometryTests COMMAND DraconicGeometryTests)
