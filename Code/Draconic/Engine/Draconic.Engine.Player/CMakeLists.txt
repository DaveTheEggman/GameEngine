# Draconic.Engine.Player - the generic game runner (engine + project content + game script; no native
# game code required). See docs/design/roadmap.md "Milestone: MVP-to-Export".
#
# One runner (PlayerApplication.h), two entry points: Main.cpp on desktop (SDL shell + a device +
# the blocking runner + CLI args) and WebMain.cpp in the browser (web shell + WebGPU + the rAF
# runner, dist PRELOADED into the virtual FS). The engine/resource/script libraries are shared.

if(EMSCRIPTEN)

    # --- Browser player: WebMain.cpp + the web platform trio + preloaded content ---
    add_executable(Draconic.Engine.Player WebMain.cpp)
    target_link_libraries(Draconic.Engine.Player PRIVATE
        Draconic::Core
        Draconic::Runtime Draconic::Runtime.Client Draconic::Runtime.Web Draconic::Engine.DefaultApp
        Draconic::Shell.Web Draconic::Shell Draconic::Graphics Draconic::Graphics.Gpu
        Draconic::Engine.Scene Draconic::Scene.Resource
        Draconic::Engine.Render Draconic::Engine.Animation Draconic::Engine.Particles
        Draconic::Resource Draconic::Geometry.Resource Draconic::Materials.Resource Draconic::Texture.Resource
        Draconic::Animation.Resource Draconic::Particles.Resource Draconic::Image.Resource
        Draconic::Model.Resource
        Draconic::Script
        Draconic::Input Draconic::Input.Resource Draconic::Engine.Input
        Draconic::UI.Resource Draconic::Engine.UI
        Draconic::Physics Draconic::Physics.Resource Draconic::Engine.Physics
        Draconic::Engine.Project Draconic::VFS.Pak Draconic::Content Draconic::VFS Draconic::Xml.Serialization)

    # The engine WGSL shader pack: cooked with the NATIVE tool (the browser has no shader compiler)
    # and preloaded at /shaders.dpak, where RenderSubsystem::LoadCookedShaderPack finds it. This is
    # SEPARATE from the game dist - the dist carries project assets, not engine shaders. (Same shape
    # as Code/Samples/WebScene.) The native Draconic.Tools.ShaderPack must be built first (desktop).
    set(_web_shader_pack "${CMAKE_CURRENT_BINARY_DIR}/shaders.dpak")
    set(_cook_tool
        "${DRACONIC_HOST_BIN_DIR}/${DRACONIC_SHADERPACKCOOK_BASENAME}${DRACONIC_HOST_EXE_SUFFIX}")
    file(GLOB _engine_shader_sources
        "${CMAKE_SOURCE_DIR}/Data/Shaders/*.hlsl" "${CMAKE_SOURCE_DIR}/Data/Shaders/*.hlsli")
    add_custom_command(
        OUTPUT "${_web_shader_pack}"
        COMMAND "${_cook_tool}" "${CMAKE_SOURCE_DIR}/Data/Shaders" "${_web_shader_pack}" wgsl
        DEPENDS ${_engine_shader_sources} "${_cook_tool}"
        COMMENT "Draconic.Engine.Player(web): cooking WGSL shaders.dpak (native Draconic.Tools.ShaderPack)")
    add_custom_target(Draconic.Engine.Player.ShaderPack DEPENDS "${_web_shader_pack}")
    add_dependencies(Draconic.Engine.Player Draconic.Engine.Player.ShaderPack)

    # The cooked game DIST to bundle (Content.pak + player.xml). Overridable so any exported dist can
    # be baked in; defaults to the staged EditorProject dist. Both files preload into the MEMFS root,
    # where the runner (projectDir ".") finds them.
    set(DRACONIC_WEB_PLAYER_DIST
        "${DRACONIC_HOST_BIN_DIR}/EditorProject/Dist/${DRACONIC_HOST_PLATFORM}"
        CACHE PATH "Cooked dist dir (Content.pak + player.xml) to bundle into the web player")

    # Full-window canvas shell, the player's own (shipping-facing: <title>Draconic Player</title>, its
    # <canvas id=canvas> matches WebShell's default selector; DPR-aware resize drives the swapchain).
    set(_web_shell "${CMAKE_CURRENT_SOURCE_DIR}/shell.html")
    set_source_files_properties(WebMain.cpp PROPERTIES OBJECT_DEPENDS "${_web_shell}")
    target_link_options(Draconic.Engine.Player PRIVATE
        "--shell-file=${_web_shell}"
        "-sASYNCIFY"               # device bring-up + per-frame GPU fence waits yield to the browser
        "-sASYNCIFY_STACK_SIZE=1048576" # the deep render-frame pump overflows the 4KB default
        "--preload-file=${_web_shader_pack}@shaders.dpak"
        "--preload-file=${DRACONIC_WEB_PLAYER_DIST}/Content.pak@Content.pak"
        "--preload-file=${DRACONIC_WEB_PLAYER_DIST}/player.xml@player.xml")
    set_target_properties(Draconic.Engine.Player PROPERTIES SUFFIX ".html")

else()

    # --- Desktop player: Main.cpp + SDL shell + a device + the blocking runner ---
    add_executable(Draconic.Engine.Player Main.cpp)
    target_link_libraries(Draconic.Engine.Player PRIVATE
        Draconic::Core
        Draconic::Runtime Draconic::Runtime.Client Draconic::Runtime.Desktop Draconic::Engine.DefaultApp
        Draconic::Shell.Desktop Draconic::Shell Draconic::Graphics Draconic::Graphics.Gpu
        Draconic::Engine.Scene Draconic::Scene.Resource
        Draconic::Engine.Render Draconic::Engine.Animation Draconic::Engine.Particles
        Draconic::Resource Draconic::Geometry.Resource Draconic::Materials.Resource Draconic::Texture.Resource
        Draconic::Animation.Resource Draconic::Particles.Resource Draconic::Image.Resource
        Draconic::Model.Resource
        Draconic::Script
        Draconic::Input Draconic::Input.Resource Draconic::Engine.Input
        Draconic::UI.Resource Draconic::Engine.UI
        Draconic::Physics Draconic::Physics.Resource Draconic::Engine.Physics
        Draconic::Engine.Project Draconic::VFS.Pak Draconic::Content Draconic::VFS Draconic::Xml.Serialization)

    # Stage the player's runtime deps beside it + emit Draconic.Engine.Player.runtime-libs (the export
    # host-template reads that list for its sidecars). SDL3 rides in as EXTRA (no-op when
    # DRACONIC_SDL3_DLL is unset); DXC (libdxcompiler.so / dxcompiler.dll) is dlopen'd at runtime by
    # the renderer's shader compiler and is NOT a link dependency, so it must be staged explicitly -
    # without it a copied dist finds no DXC and the renderer stays inert. On Linux, $ORIGIN on the
    # player's RUNPATH lets the loader find the staged copy beside the exe (Windows searches the exe
    # dir for a bare DLL by default).
    draconic_copy_runtime_deps(Draconic.Engine.Player EXTRA ${DRACONIC_SDL3_DLL} ${DRACONIC_DXC_LIB})
    if(UNIX AND NOT APPLE)
        set_property(TARGET Draconic.Engine.Player APPEND PROPERTY BUILD_RPATH "$ORIGIN")
        set_property(TARGET Draconic.Engine.Player PROPERTY INSTALL_RPATH "$ORIGIN")
    endif()

endif()

set_target_properties(Draconic.Engine.Player PROPERTIES OUTPUT_NAME ${DRACONIC_PLAYER_BASENAME})
