# Draconic::RenderSubsystem unit tests (doctest; extraction is pure data, no GPU).

add_executable(DraconicRenderSubsystemTests
    ResourceRefTests.cpp
    TestsMain.cpp
    ExtractTests.cpp)

target_link_libraries(DraconicRenderSubsystemTests PRIVATE
    Draconic::RenderSubsystem Draconic::MeshResource Draconic::SceneResource
    Draconic::Resource Draconic::VFS Draconic::Content doctest::doctest)

target_compile_definitions(DraconicRenderSubsystemTests PRIVATE
    DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS)

target_compile_options(DraconicRenderSubsystemTests PRIVATE
    "$<$<CXX_COMPILER_ID:Clang>:-Wno-error=#warnings;-Wno-error=c2y-extensions>"
    "$<$<CXX_COMPILER_ID:GNU>:-Wno-error=cpp>")

add_test(NAME DraconicRenderSubsystemTests COMMAND DraconicRenderSubsystemTests)
