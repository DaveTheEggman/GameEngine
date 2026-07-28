add_executable(DraconicEditorAudioTests TestsMain.cpp BusLayoutPageTests.cpp)
target_link_libraries(DraconicEditorAudioTests PRIVATE
    Draconic::EditorAudio Draconic::EditorCore
    Draconic::Audio Draconic::AudioEditor Draconic::Content doctest::doctest)
target_compile_definitions(DraconicEditorAudioTests PRIVATE DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS)
target_compile_options(DraconicEditorAudioTests PRIVATE
    "$<$<CXX_COMPILER_ID:Clang>:-Wno-error=#warnings>" "$<$<CXX_COMPILER_ID:GNU>:-Wno-error=cpp>")
add_test(NAME DraconicEditorAudioTests COMMAND DraconicEditorAudioTests)
