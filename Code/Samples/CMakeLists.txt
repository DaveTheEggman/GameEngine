# Draconic sample applications. Built when their required backends are available.

# Stage runtime deps (SDL3.dll + any other linked DLLs) next to a sample exe.
# Thin forwarder to the global draconic_copy_runtime_deps so the ~40 sample call
# sites need no change; the vendored SDL3 DLL rides in as EXTRA (a no-op when
# DRACONIC_SDL3_DLL is unset, e.g. on Linux).
function(draconic_copy_sdl3_dll target)
    draconic_copy_runtime_deps(${target} EXTRA ${DRACONIC_SDL3_DLL})
endfunction()

# NetEcho - console proof of draconic.net (no graphics); builds + runs anywhere.
add_subdirectory(NetEcho)

# HelloWindow and MultiWindow both present through a GraphicsDevice, so both need
# the desktop shell + the Vulkan GPU factory.
if(TARGET Draconic::Shell.Desktop AND TARGET Draconic::Runtime.Desktop AND TARGET Draconic::Graphics.Gpu)
    add_subdirectory(HelloWindow)
    add_subdirectory(InputActions)       # draconic.input consumer proof (named actions end-to-end)
    add_subdirectory(PhysicsPlayground)  # physics P1 consumer proof (crates/trigger/raycast on Jolt)
    add_subdirectory(ScriptPlayground)   # script P1 consumer proof (Wren Mover/Spinner behaviors on the ScriptSubsystem)
    add_subdirectory(AudioPlayground)    # audio P1 consumer proof (ambient loop + positional one-shots on miniaudio)
    add_subdirectory(MultiWindow)
    add_subdirectory(RenderStressTest)   # worst-case benchmark to keep perf honest
    # These dev harnesses link the authoring pipeline (ModelImporter / *.Editor cook libs), so they
    # only build when the tooling is present (DRACONIC_BUILD_TOOLS; always off for web).
    if(DRACONIC_BUILD_TOOLS)
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
if(TARGET Draconic::SamplesFramework)
    add_subdirectory(RHI)
    add_subdirectory(VG)  # VG sandbox - end-to-end VG stack demo
    add_subdirectory(GUI) # GUI sandbox - draconic.gui widgets + CSS on the VG renderer
    add_subdirectory(UI)  # UI sandbox - draconic.ui (Sedulous.UI port) on the VG renderer
endif()
