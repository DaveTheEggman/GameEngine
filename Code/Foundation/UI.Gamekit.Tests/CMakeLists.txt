add_executable(UI.Gamekit.Tests
    ScreenStackTests.cpp
    TestsMain.cpp)

target_link_libraries(UI.Gamekit.Tests PRIVATE
    Foundation::UI.Gamekit Foundation::UI Foundation::Core doctest::doctest)

target_compile_definitions(UI.Gamekit.Tests PRIVATE
    DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS)

target_compile_options(UI.Gamekit.Tests PRIVATE
    "$<$<CXX_COMPILER_ID:Clang>:-Wno-error=#warnings;-Wno-error=c2y-extensions>"
    "$<$<CXX_COMPILER_ID:GNU>:-Wno-error=cpp>")

add_test(NAME UI.Gamekit.Tests COMMAND UI.Gamekit.Tests)
