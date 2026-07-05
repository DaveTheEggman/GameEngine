# Draconic::Render unit tests (doctest; Null RHI — no real GPU).

add_executable(DraconicRenderTests
    TestsMain.cpp
    GpuMeshTests.cpp
    RenderDataTests.cpp
    PipelineTests.cpp)

target_link_libraries(DraconicRenderTests PRIVATE
    Draconic::Render Draconic::RHINull doctest::doctest)

target_compile_definitions(DraconicRenderTests PRIVATE
    DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS)

target_compile_options(DraconicRenderTests PRIVATE
    "$<$<CXX_COMPILER_ID:Clang>:-Wno-error=#warnings;-Wno-error=c2y-extensions>"
    "$<$<CXX_COMPILER_ID:GNU>:-Wno-error=cpp>")

add_test(NAME DraconicRenderTests COMMAND DraconicRenderTests)
