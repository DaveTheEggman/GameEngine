add_executable(Engine.UI.Script.Tests
    UiScriptHandleTests.cpp
    TestsMain.cpp)

target_link_libraries(Engine.UI.Script.Tests PRIVATE
    Engine::UI.Script Foundation::UI Foundation::Core doctest::doctest)

target_compile_definitions(Engine.UI.Script.Tests PRIVATE
    DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS)

target_compile_options(Engine.UI.Script.Tests PRIVATE
    "$<$<CXX_COMPILER_ID:Clang>:-Wno-error=#warnings;-Wno-error=c2y-extensions>"
    "$<$<CXX_COMPILER_ID:GNU>:-Wno-error=cpp>")

add_test(NAME Engine.UI.Script.Tests COMMAND Engine.UI.Script.Tests)
