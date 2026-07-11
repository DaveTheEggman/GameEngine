# RaptorEditor - the Draconic editor executable (assembly of draconic.editor.app + the
# per-subsystem editor plugin modules; see docs/design/editor.md §3.1).
add_executable(RaptorEditor Main.cpp)
target_link_libraries(RaptorEditor PRIVATE
    Draconic::Core
    Draconic::Runtime Draconic::RuntimeClient Draconic::RuntimeDesktop Draconic::ShellDesktop
    Draconic::Shell Draconic::Graphics Draconic::GraphicsGpu
    Draconic::EditorCore Draconic::EditorApp)
target_compile_definitions(RaptorEditor PRIVATE
    DRACONIC_EDITOR_FONT_PATH="${CMAKE_SOURCE_DIR}/Data/Assets/fonts/roboto/Roboto-Regular.ttf")
# Copy SDL3.dll next to the executable on Windows (same as the samples' helper, which is
# defined in Code/Samples/ and not visible here).
if(DRACONIC_SDL3_DLL)
    add_custom_command(TARGET RaptorEditor POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${DRACONIC_SDL3_DLL}" "$<TARGET_FILE_DIR:RaptorEditor>")
endif()
