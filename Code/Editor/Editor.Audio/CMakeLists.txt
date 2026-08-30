# Editor::Audio - `editor.audio`: audio clip audition page (waveform + play).
add_library(Editor.Audio STATIC)
add_library(Editor::Audio ALIAS Editor.Audio)
target_link_libraries(Editor.Audio PUBLIC
    Foundation::Core Foundation::Content Foundation::Runtime.Client Foundation::UI Foundation::UI.Toolkit
    Foundation::Audio Pipeline::Audio Engine::Audio Foundation::Image Foundation::Image.IO Foundation::VFS
    Editor::Core Editor::App Foundation::Policy)
target_sources(Editor.Audio PRIVATE SoundCuePageImpl.cpp AudioClipPageImpl.cpp BusLayoutPageImpl.cpp PUBLIC FILE_SET editor_audio_modules TYPE CXX_MODULES FILES AudioClipPage.cppm SoundCuePage.cppm BusLayoutPage.cppm AudioClipThumbnailGenerator.cppm)

