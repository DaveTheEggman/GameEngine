add_executable(DraconicAudioSceneTests
    TestsMain.cpp
    AudioSceneTests.cpp)

target_link_libraries(DraconicAudioSceneTests PRIVATE
    Engine::Audio Foundation::Audio.Resource Foundation::Scene Foundation::Scene.Resource
    Foundation::Content Foundation::Resource Foundation::VFS
    Foundation::Script Foundation::Script.Wren doctest::doctest)

target_compile_definitions(DraconicAudioSceneTests PRIVATE
    DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS)

target_compile_options(DraconicAudioSceneTests PRIVATE
    "$<$<CXX_COMPILER_ID:Clang>:-Wno-error=#warnings;-Wno-error=c2y-extensions>"
    "$<$<CXX_COMPILER_ID:GNU>:-Wno-error=cpp>")

add_test(NAME DraconicAudioSceneTests COMMAND DraconicAudioSceneTests)
