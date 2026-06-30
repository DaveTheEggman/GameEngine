# Draconic::Render unit tests (doctest; Null RHI — no real GPU).
if(NOT TARGET doctest::doctest)
    include(FetchContent)
    FetchContent_Declare(doctest
        GIT_REPOSITORY https://github.com/doctest/doctest.git
        GIT_TAG v2.4.11
        GIT_SHALLOW TRUE
        SYSTEM)
    set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
    FetchContent_MakeAvailable(doctest)
    unset(CMAKE_POLICY_VERSION_MINIMUM)
endif()

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
