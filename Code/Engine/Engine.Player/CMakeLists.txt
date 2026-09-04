# Engine.Player - the generic game runner (engine + project content + game script; no native
# game code required).
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

    # Full-window canvas shell, the player's own (shipping-facing: <title>Player</title>, its
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

    # --- Desktop player: launcher-as-library (game-native-code.md N1) ---
    # The whole boot is Engine.Player.Main (a STATIC lib in every mode - it carries main
    # logic, not engine state); the dev executable is a thin stub calling
    # PlayerMain(nullptr), and the exporter's generated SHIP stub links this same library
    # with the game's static plugin. Deliberately NOT util_add_engine_library.
    add_library(Engine.Player.Main STATIC)
    target_sources(Engine.Player.Main
        PUBLIC FILE_SET player_main_modules TYPE CXX_MODULES FILES PlayerMain.cppm
        PRIVATE PlayerMain.cpp)
    target_link_libraries(Engine.Player.Main PRIVATE
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

    add_executable(Engine.Player Main.cpp)
    target_link_libraries(Engine.Player PRIVATE Engine.Player.Main Foundation::Policy)

    # The SHIP player (game-native-code.md N3): only when a game's native directory is
    # wired into this build (ENGINE_GAME_NATIVE_DIR/_TARGET - the exporter's seam, also
    # usable by hand). Links the game's plugin STATICALLY through the same launcher;
    # ShipMain's CreatePlugin reference pulls the registration from the game archive.
    if(ENGINE_GAME_NATIVE_TARGET)
        add_executable(Engine.GamePlayer ShipMain.cpp)
        target_link_libraries(Engine.GamePlayer PRIVATE
            Engine.Player.Main ${ENGINE_GAME_NATIVE_TARGET} Foundation::Policy)
        util_copy_runtime_deps(Engine.GamePlayer EXTRA ${BUILDSYSTEM_DXC_LIB} ${BUILDSYSTEM_WGPU_LIB})
        if(UNIX AND NOT APPLE)
            set_property(TARGET Engine.GamePlayer APPEND PROPERTY BUILD_RPATH "$ORIGIN")
            set_property(TARGET Engine.GamePlayer PROPERTY INSTALL_RPATH "$ORIGIN")
        endif()
    endif()

    # Stage the player's runtime deps beside it + emit Engine.Player.runtime-libs (the export
    # host-template reads that list for its sidecars). SDL3 links statically (no DLL to stage);
    # DXC (libdxcompiler.so / dxcompiler.dll) is dlopen'd at runtime by
    # the renderer's shader compiler and is NOT a link dependency, so it must be staged explicitly -
    # without it a copied dist finds no DXC and the renderer stays inert. On Linux, $ORIGIN on the
    # player's RUNPATH lets the loader find the staged copy beside the exe (Windows searches the exe
    # dir for a bare DLL by default).
    # wgpu-native rides along for the same reason (dlopen dependency; --webgpu in a dist).
    util_copy_runtime_deps(Engine.Player EXTRA ${BUILDSYSTEM_DXC_LIB} ${BUILDSYSTEM_WGPU_LIB})
    if(UNIX AND NOT APPLE)
        set_property(TARGET Engine.Player APPEND PROPERTY BUILD_RPATH "$ORIGIN")
        set_property(TARGET Engine.Player PROPERTY INSTALL_RPATH "$ORIGIN")
    endif()

endif()

set_target_properties(Engine.Player PROPERTIES OUTPUT_NAME ${BUILTIN_PLAYER_BASENAME})
