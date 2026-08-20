# The tests REGISTER the AngelScript cook (an entry point's job) - the library under test
# stays backend-neutral and resolves through the registry (B3). The AngelScript cook's OWN
# suite, so it builds only when AngelScript is on.
if(OPTION_ENABLE_ANGELSCRIPT)
    add_executable(Script.AngelScript.Pipeline.Tests TestsMain.cpp AngelScriptCookTests.cpp)
    target_link_libraries(Script.AngelScript.Pipeline.Tests PRIVATE
        Pipeline::Script.AngelScript
        Foundation::Script.AngelScript Pipeline::Script
        Foundation::Script.Resource Foundation::Script Foundation::Script.Facades
        Engine::ScriptSurface # RegisterAllScriptFacades: the COMPLETE engine facade surface the
                              # run:: / ui:: regression cook compiles against
        doctest::doctest)
    target_compile_definitions(Script.AngelScript.Pipeline.Tests PRIVATE DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS)
    target_compile_options(Script.AngelScript.Pipeline.Tests PRIVATE
        "$<$<CXX_COMPILER_ID:Clang>:-Wno-error=#warnings;-Wno-error=c2y-extensions>"
        "$<$<CXX_COMPILER_ID:GNU>:-Wno-error=cpp>")
    add_test(NAME Script.AngelScript.Pipeline.Tests COMMAND Script.AngelScript.Pipeline.Tests)
endif()
