add_executable(Draconic.Editor.Audio.Tests TestsMain.cpp BusLayoutPageTests.cpp)
target_link_libraries(Draconic.Editor.Audio.Tests PRIVATE
    Draconic::EditorAudio Draconic::EditorCore
    Draconic::Audio Draconic::AudioEditor Draconic::Content doctest::doctest)
target_compile_definitions(Draconic.Editor.Audio.Tests PRIVATE DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS)
target_compile_options(Draconic.Editor.Audio.Tests PRIVATE
    "$<$<CXX_COMPILER_ID:Clang>:-Wno-error=#warnings>" "$<$<CXX_COMPILER_ID:GNU>:-Wno-error=cpp>")
add_test(NAME Draconic.Editor.Audio.Tests COMMAND Draconic.Editor.Audio.Tests)
