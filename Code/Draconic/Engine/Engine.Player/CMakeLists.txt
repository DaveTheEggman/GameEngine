# Engine.Player - the generic game runner (engine + project content + game script; no native
# game code required). See docs/design/roadmap.md "Milestone: MVP-to-Export".
#
# One runner (PlayerApplication.h), two entry points: Main.cpp on desktop (SDL shell + a device +
# the blocking runner + CLI args) and WebMain.cpp in the browser (web shell + WebGPU + the rAF
# runner, dist PRELOADED into the virtual FS). The engine/resource/script libraries are shared.

if(EMSCRIPTEN)

    # --- Browser player: WebMain.cpp + the web platform trio + preloaded content ---
    add_executable(Engine.Player WebMain.cpp)
    target_link_libraries(Engine.Player PRIVATE
        Foundation::Core
        Foundation::Runtime Foundation::Runtime.Client Foundation::Runtime.Web Engine::DefaultApp
        Foundation::Shell.Web Foundation::Shell Foundation::Graphics Foundation::Graphics.Gpu
        Engine::Scene Foundation::Scene.Resource
        Engine::Render Engine::Animation Engine::Particles
        Foundation::Resource Foundation::Geometry.Resource Foundation::Materials.Resource Foundation::Texture.Resource
        Foundation::Animation.Resource Foundation::Particles.Resource Foundation::Image.Resource
        Foundation::Fonts Foundation::Fonts.Resource
        Foundation::Model.Resource
        Foundation::Script
        Foundation::Input Foundation::Input.Resource Engine::Input
        Foundation::UI.Resource Engine::UI
        Foundation::Physics Foundation::Physics.Resource Engine::Physics
        Engine::Project Foundation::VFS.Pak Foundation::Content Foundation::VFS Foundation::Xml.Serialization)

    # NOTHING is baked into this binary: the player FETCHES player.xml + Content.pak (the export
    # output) and shaders.dpak (the export-cooked WGSL engine pack) from the serving folder at
    # startup (WebMain.cpp, synchronous under ASYNCIFY). That makes the build a reusable EXPORT
    # TEMPLATE - export any project, drop its files beside the player, serve, browse - and the
    # web build no longer needs a native cook tool or a pre-exported dist to CONFIGURE.

    # Full-window canvas shell, the player's own (shipping-facing: <title>Draconic Player</title>, its
    # <canvas id=canvas> matches WebShell's default selector; DPR-aware resize drives the swapchain).
    set(_web_shell "${CMAKE_CURRENT_SOURCE_DIR}/shell.html")
    set_source_files_properties(WebMain.cpp PROPERTIES OBJECT_DEPENDS "${_web_shell}")
    target_link_options(Engine.Player PRIVATE
        "--shell-file=${_web_shell}"
        "-sASYNCIFY"               # device bring-up + per-frame GPU fence waits yield to the browser
        "-sASYNCIFY_STACK_SIZE=1048576" # the deep render-frame pump overflows the 4KB default
    )
    set_target_properties(Engine.Player PROPERTIES SUFFIX ".html")

    # Stage serve.py (COOP/COEP + wasm MIME + no-store static server) beside the output, and emit
    # the runtime-libs manifest the export template machinery reads: `--template create <this build
    # dir> --install` packages the player page + these sidecars as an importable "Web" template.
    set(_web_out_dir "$<TARGET_FILE_DIR:Engine.Player>")
    add_custom_command(TARGET Engine.Player POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "${CMAKE_CURRENT_SOURCE_DIR}/serve.py" "${_web_out_dir}/serve.py"
        COMMENT "Engine.Player(web): staging serve.py"
        VERBATIM)
    file(GENERATE OUTPUT "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/Engine.Player.runtime-libs"
         CONTENT "Engine.Player.js\nEngine.Player.wasm\nserve.py\n")

else()

    # --- Desktop player: Main.cpp + SDL shell + a device + the blocking runner ---
    add_executable(Engine.Player Main.cpp)
    target_link_libraries(Engine.Player PRIVATE
        Foundation::Core
        Foundation::Runtime Foundation::Runtime.Client Foundation::Runtime.Desktop Engine::DefaultApp
        Foundation::Shell.Desktop Foundation::Shell Foundation::Graphics Foundation::Graphics.Gpu
        Engine::Scene Foundation::Scene.Resource
        Engine::Render Engine::Animation Engine::Particles
        Foundation::Resource Foundation::Geometry.Resource Foundation::Materials.Resource Foundation::Texture.Resource
        Foundation::Animation.Resource Foundation::Particles.Resource Foundation::Image.Resource
        Foundation::Fonts Foundation::Fonts.Resource
        Foundation::Model.Resource
        Foundation::Script
        Foundation::Input Foundation::Input.Resource Engine::Input
        Foundation::UI.Resource Engine::UI
        Foundation::Physics Foundation::Physics.Resource Engine::Physics
        Engine::Project Foundation::VFS.Pak Foundation::Content Foundation::VFS Foundation::Xml.Serialization)

    # Stage the player's runtime deps beside it + emit Engine.Player.runtime-libs (the export
    # host-template reads that list for its sidecars). SDL3 rides in as EXTRA (no-op when
    # BUILDSYSTEM_SDL3_DLL is unset); DXC (libdxcompiler.so / dxcompiler.dll) is dlopen'd at runtime by
    # the renderer's shader compiler and is NOT a link dependency, so it must be staged explicitly -
    # without it a copied dist finds no DXC and the renderer stays inert. On Linux, $ORIGIN on the
    # player's RUNPATH lets the loader find the staged copy beside the exe (Windows searches the exe
    # dir for a bare DLL by default).
    util_copy_runtime_deps(Engine.Player EXTRA ${BUILDSYSTEM_SDL3_DLL} ${BUILDSYSTEM_DXC_LIB})
    if(UNIX AND NOT APPLE)
        set_property(TARGET Engine.Player APPEND PROPERTY BUILD_RPATH "$ORIGIN")
        set_property(TARGET Engine.Player PROPERTY INSTALL_RPATH "$ORIGIN")
    endif()

endif()

set_target_properties(Engine.Player PROPERTIES OUTPUT_NAME ${BUILTIN_PLAYER_BASENAME})
