# WebScene - the first FULL engine app in a browser (DefaultApplication + a lit PBR scene on the web
# runner). Emscripten-only. Links the app stack (WebShell + web runner + WebGPU factory + the
# subsystem libraries) and bundles a cooked WGSL shaders.dpak so the renderer loads it at runtime
# (no compiler in the browser). ASYNCIFY is required for the synchronous device bring-up.
if(NOT EMSCRIPTEN)
    return()
endif()

add_executable(WebScene main.cpp)

target_link_libraries(WebScene PRIVATE
    Draconic::Core
    Draconic::Runtime
    Draconic::Runtime.Client
    Draconic::Shell.Web
    Draconic::Runtime.Web
    Draconic::Graphics
    Draconic::Graphics.Gpu
    Draconic::Engine.DefaultApp
    Draconic::Scene
    Draconic::Engine.Scene
    Draconic::Engine.Render
    Draconic::Geometry
    Draconic::Materials
    Draconic::Policy)

# Cook the WGSL shader pack with the NATIVE tool (the browser has no compiler) and bundle it into the
# virtual FS at /shaders.dpak, where RenderSubsystem::LoadCookedShaderPack finds it. Draconic.Tools.ShaderPack
# must be built first by any desktop preset (it is a native authoring tool, never built for wasm).
set(_web_shader_pack "${CMAKE_CURRENT_BINARY_DIR}/shaders.dpak")
set(_cook_tool
    "${DRACONIC_HOST_BIN_DIR}/${DRACONIC_SHADERPACKCOOK_BASENAME}${DRACONIC_HOST_EXE_SUFFIX}")
file(GLOB _engine_shader_sources CONFIGURE_DEPENDS
    "${CMAKE_SOURCE_DIR}/Data/Shaders/*.hlsl" "${CMAKE_SOURCE_DIR}/Data/Shaders/*.hlsli")
add_custom_command(
    OUTPUT "${_web_shader_pack}"
    COMMAND "${_cook_tool}" "${CMAKE_SOURCE_DIR}/Data/Shaders" "${_web_shader_pack}" wgsl
    DEPENDS ${_engine_shader_sources} "${_cook_tool}"
    COMMENT "WebScene: cooking WGSL shaders.dpak (native Draconic.Tools.ShaderPack)"
    VERBATIM)
add_custom_target(WebSceneShaderPack DEPENDS "${_web_shader_pack}")
add_dependencies(WebScene WebSceneShaderPack)

# Full-window canvas: a custom HTML shell sizes the <canvas> to the viewport (the default Emscripten
# shell is a small fixed canvas). Relinks when the shell changes.
set(_web_shell "${CMAKE_CURRENT_SOURCE_DIR}/shell.html")
set_source_files_properties(main.cpp PROPERTIES OBJECT_DEPENDS "${_web_shell}")

target_link_options(WebScene PRIVATE
    "--shell-file=${_web_shell}"
    "-sASYNCIFY"              # synchronous device init + per-frame GPU fence waits yield to the browser
    # ASYNCIFY unwinds the WHOLE C++ stack into this buffer on every emscripten_sleep. The full
    # renderer's GPU-completion pump (PumpUntilWithDevice) yields from deep inside the render frame
    # (app -> render subsystem -> render frame -> passes -> RHI -> WebGPU), which overflows the 4 KB
    # default and traps as "unreachable". 1 MB comfortably fits the deepest render path.
    "-sASYNCIFY_STACK_SIZE=1048576"
    "-sALLOW_MEMORY_GROWTH=1"
    "-sASSERTIONS=1"
    "--preload-file=${_web_shader_pack}@shaders.dpak")

set_target_properties(WebScene PROPERTIES SUFFIX ".html")
