# The tests REGISTER the Luau cook (an entry point's job) - the library under test stays
# backend-neutral and resolves through the registry (B3). Cook-only suite (no editor-UI dep).
# The Luau cook's OWN suite, so it builds only when Luau is on.
if(OPTION_ENABLE_LUAU)
    add_executable(Script.Luau.Pipeline.Tests TestsMain.cpp LuauCookTests.cpp)
    target_link_libraries(Script.Luau.Pipeline.Tests PRIVATE
        Pipeline::Script.Luau
        Foundation::Script.Luau Pipeline::Script
        Foundation::Script.Resource Foundation::Script Foundation::Script.Facades
        doctest::doctest)
    target_compile_definitions(Script.Luau.Pipeline.Tests PRIVATE DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS)
    target_compile_options(Script.Luau.Pipeline.Tests PRIVATE
        "$<$<CXX_COMPILER_ID:Clang>:-Wno-error=#warnings;-Wno-error=c2y-extensions>"
        "$<$<CXX_COMPILER_ID:GNU>:-Wno-error=cpp>")
    add_test(NAME Script.Luau.Pipeline.Tests COMMAND Script.Luau.Pipeline.Tests)
endif()
