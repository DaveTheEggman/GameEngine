# Draconic::RenderSubsystem unit tests (doctest; extraction is pure data, no GPU).

add_executable(Draconic.Engine.Render.Tests
    ResourceRefTests.cpp
    TestsMain.cpp
    ExtractTests.cpp)

target_link_libraries(Draconic.Engine.Render.Tests PRIVATE
    Draconic::RenderSubsystem Draconic::MeshResource Draconic::MaterialResource Draconic::SceneResource
    Draconic::Resource Draconic::VFS Draconic::Content doctest::doctest)

target_compile_definitions(Draconic.Engine.Render.Tests PRIVATE
    DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS)

target_compile_options(Draconic.Engine.Render.Tests PRIVATE
    "$<$<CXX_COMPILER_ID:Clang>:-Wno-error=#warnings;-Wno-error=c2y-extensions>"
    "$<$<CXX_COMPILER_ID:GNU>:-Wno-error=cpp>")

add_test(NAME Draconic.Engine.Render.Tests COMMAND Draconic.Engine.Render.Tests)
