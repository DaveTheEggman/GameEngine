# Raptor::RenderGraph unit tests (doctest).
if(NOT TARGET doctest::doctest)
    include(FetchContent)
    FetchContent_Declare(doctest GIT_REPOSITORY https://github.com/doctest/doctest.git
        GIT_TAG v2.4.11 GIT_SHALLOW TRUE SYSTEM)
    set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
    FetchContent_MakeAvailable(doctest)
    unset(CMAKE_POLICY_VERSION_MINIMUM)
endif()

add_executable(RaptorRenderGraphTests TestsMain.cpp RenderGraphTests.cpp BarrierTests.cpp
    TypeTests.cpp DescriptorTests.cpp GraphCoreTests.cpp CullingTests.cpp DependencyTests.cpp PassBuilderTests.cpp ValidationTests.cpp DebugTests.cpp ProfilerTests.cpp)
target_link_libraries(RaptorRenderGraphTests PRIVATE Raptor::RenderGraph doctest::doctest)
target_compile_definitions(RaptorRenderGraphTests PRIVATE DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS)
target_compile_options(RaptorRenderGraphTests PRIVATE
    "$<$<CXX_COMPILER_ID:Clang>:-Wno-error=#warnings;-Wno-error=c2y-extensions>" "$<$<CXX_COMPILER_ID:GNU>:-Wno-error=cpp>")
add_test(NAME RaptorRenderGraphTests COMMAND RaptorRenderGraphTests)
