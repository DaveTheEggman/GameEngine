# Draconic::Materials unit tests (doctest; Null RHI — no GPU/DXC).

add_executable(DraconicMaterialsTests
    TestsMain.cpp
    MaterialsTests.cpp)

target_link_libraries(DraconicMaterialsTests PRIVATE
    Draconic::Materials Draconic::RHINull doctest::doctest)

target_compile_definitions(DraconicMaterialsTests PRIVATE
    DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS)

target_compile_options(DraconicMaterialsTests PRIVATE
    "$<$<CXX_COMPILER_ID:Clang>:-Wno-error=#warnings;-Wno-error=c2y-extensions>"
    "$<$<CXX_COMPILER_ID:GNU>:-Wno-error=cpp>")

add_test(NAME DraconicMaterialsTests COMMAND DraconicMaterialsTests)
