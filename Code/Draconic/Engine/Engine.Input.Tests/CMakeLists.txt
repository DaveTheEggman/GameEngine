# Draconic::Engine.Input unit tests (doctest): the InputSubsystem (per-surface scene binding /
# source overrides) + the per-context Wren Input facade. Split out of Foundation/Input.Tests so
# the Foundation input tests don't link Engine. Includes the shared synthetic-device helpers from
# the foundation test dir (header-only test scaffolding - no link dependency).

add_executable(Draconic.Engine.Input.Tests
    TestsMain.cpp
    InputEngineTests.cpp)

target_link_libraries(Draconic.Engine.Input.Tests PRIVATE
    Draconic::Engine.Input Draconic::Input Draconic::Shell Draconic::Settings
    Draconic::Script Draconic::Script.Wren doctest::doctest)

target_include_directories(Draconic.Engine.Input.Tests PRIVATE
    ${CMAKE_SOURCE_DIR}/Code/Draconic/Foundation/Input.Tests)  # InputTestSupport.h

target_compile_definitions(Draconic.Engine.Input.Tests PRIVATE
    DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS)

target_compile_options(Draconic.Engine.Input.Tests PRIVATE
    "$<$<CXX_COMPILER_ID:Clang>:-Wno-error=#warnings;-Wno-error=c2y-extensions>"
    "$<$<CXX_COMPILER_ID:GNU>:-Wno-error=cpp>")

add_test(NAME Draconic.Engine.Input.Tests COMMAND Draconic.Engine.Input.Tests)
