# Draconic::Engine.Animation unit tests (doctest; components are pure data, no GPU).

add_executable(Draconic.Engine.Animation.Tests
    ResourceRefTests.cpp
    TestsMain.cpp)

target_link_libraries(Draconic.Engine.Animation.Tests PRIVATE
    Draconic::Engine.Animation Draconic::Animation.Resource Draconic::Scene.Resource
    Draconic::Resource Draconic::VFS Draconic::Content doctest::doctest)

target_compile_definitions(Draconic.Engine.Animation.Tests PRIVATE
    DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS)

target_compile_options(Draconic.Engine.Animation.Tests PRIVATE
    "$<$<CXX_COMPILER_ID:Clang>:-Wno-error=#warnings;-Wno-error=c2y-extensions>"
    "$<$<CXX_COMPILER_ID:GNU>:-Wno-error=cpp>")

add_test(NAME Draconic.Engine.Animation.Tests COMMAND Draconic.Engine.Animation.Tests)
