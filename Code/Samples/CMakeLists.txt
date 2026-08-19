# Sample applications. Built when their required backends are available.

# Stage runtime deps (SDL3.dll + any other linked DLLs) next to a sample exe.
# Thin forwarder to the global util_copy_runtime_deps so the ~40 sample call
# sites need no change; the vendored SDL3 DLL rides in as EXTRA (a no-op when
# BUILDSYSTEM_SDL3_DLL is unset, e.g. on Linux).
function(util_copy_sdl3_dll target)
    util_copy_runtime_deps(${target} EXTRA ${BUILDSYSTEM_SDL3_DLL})
endfunction()

# NetEcho - console proof of foundation.net (no graphics); builds + runs anywhere.
add_subdirectory(NetEcho)

# HelloWindow and MultiWindow both present through a GraphicsDevice, so both need
# the desktop shell + the Vulkan GPU factory.
if(TARGET Foundation::Shell.Desktop AND TARGET Foundation::Runtime.Desktop AND TARGET Foundation::Graphics.Gpu)
    add_subdirectory(HelloWindow)
    add_subdirectory(ScriptPlayground)   # script P1 consumer proof (Mover/Spinner behaviors on the ScriptSubsystem)
    add_subdirectory(MultiWindow)
    # Samples with an ImGui debug UI - honor the extension option, or an
    # -DOPTION_EXTENSION_IMGUI=OFF configure dies on a missing Extensions::Imgui target.
    if(OPTION_EXTENSION_IMGUI)
        add_subdirectory(InputActions)       # foundation.input consumer proof (named actions end-to-end)
        add_subdirectory(PhysicsPlayground)  # physics P1 consumer proof (crates/trigger/raycast on Jolt)
        add_subdirectory(AudioPlayground)    # audio P1 consumer proof (ambient loop + positional one-shots on miniaudio)
        add_subdirectory(RenderStressTest)   # worst-case benchmark to keep perf honest
    endif()
    # These dev harnesses link the authoring pipeline (ModelImporter / *.Editor cook libs), so they
    # only build when the tooling is present (OPTION_BUILD_TOOLS; always off for web).
    if(OPTION_BUILD_TOOLS AND OPTION_EXTENSION_IMGUI)
        add_subdirectory(Sandbox)            # the dev harness we grow the engine in
        add_subdirectory(AnimStressTest)     # animation/skinning benchmark (Sandbox copy, stripped toward a grid)
        add_subdirectory(AnimatedCrowd)      # skinned-crowd instancing (InstancedMeshComponent + InstancedSkinning)
        add_subdirectory(ParticleFX)         # particle-system showcase (CPU sim + dedicated ParticleRenderer)
    endif()
else()
    message(STATUS "Samples: skipping HelloWindow/MultiWindow (need desktop shell + Vulkan GPU factory)")
endif()

# RHI sample framework + RHI samples (need Vulkan + Shaders too).
add_subdirectory(Framework)
if(TARGET Samples::Framework)
    add_subdirectory(RHI)
    add_subdirectory(WebScene) # full-renderer exercise scene (desktop build; also the web sample)
    add_subdirectory(VG)  # VG sandbox - end-to-end VG stack demo
    if(OPTION_EXPERIMENTAL_GUI)
        add_subdirectory(GUI) # GUI sandbox - experimental.gui widgets + CSS on the VG renderer
    endif()
    add_subdirectory(UI)  # UI sandbox - foundation.ui (Sedulous.UI port) on the VG renderer
endif()
