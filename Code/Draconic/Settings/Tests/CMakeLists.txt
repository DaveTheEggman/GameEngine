add_executable(DraconicSettingsTests TestsMain.cpp SettingsTests.cpp)
target_link_libraries(DraconicSettingsTests PRIVATE Draconic::Settings Draconic::Core doctest::doctest)
target_compile_definitions(DraconicSettingsTests PRIVATE DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS)
target_compile_options(DraconicSettingsTests PRIVATE
    "$<$<CXX_COMPILER_ID:Clang>:-Wno-error=#warnings>" "$<$<CXX_COMPILER_ID:GNU>:-Wno-error=cpp>")
add_test(NAME DraconicSettingsTests COMMAND DraconicSettingsTests)
