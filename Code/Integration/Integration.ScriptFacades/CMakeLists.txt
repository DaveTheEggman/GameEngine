# Integration.ScriptFacades - subsystem script FACADES exercised end-to-end on the script backends
# (Net.* and Ui.* driven from Wren + AngelScript). These are cross-layer tests (scripting x a
# subsystem), so they live here rather than in the subsystem's own unit-test suite - which stays
# backend-neutral. Each backend leg is #ifdef OPTION_HAS_<backend>-guarded (the global presence
# macros on policy); link only the enabled backends via the ${SCRIPTLIB_*} target-or-empty aliases.
add_executable(Integration.ScriptFacades TestsMain.cpp
    NetFacadeScriptTests.cpp UiFacadeScriptTests.cpp AudioFacadeScriptTests.cpp)
target_link_libraries(Integration.ScriptFacades PRIVATE
    Foundation::Core Foundation::Script Foundation::Net Foundation::Net.Manager Engine::UI
    Foundation::Audio Engine::Audio Foundation::Audio.Resource
    Foundation::Content Foundation::Resource Foundation::VFS
    ${SCRIPTLIB_WREN} ${SCRIPTLIB_ANGELSCRIPT}
    doctest::doctest)
target_compile_definitions(Integration.ScriptFacades PRIVATE DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS)
target_compile_options(Integration.ScriptFacades PRIVATE
    "$<$<CXX_COMPILER_ID:Clang>:-Wno-error=#warnings;-Wno-error=c2y-extensions>"
    "$<$<CXX_COMPILER_ID:GNU>:-Wno-error=cpp>")
add_test(NAME Integration.ScriptFacades COMMAND Integration.ScriptFacades)
