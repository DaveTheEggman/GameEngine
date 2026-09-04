# Reusable CMake helpers. Included once from the root CMakeLists, so the
# functions defined here are visible in every add_subdirectory() below it.

# util_copy_runtime_deps(<target> [EXTRA <file>...])
#
# POST_BUILD, stage everything <target> needs to run next to its executable:
#
#  * On Windows, every runtime DLL CMake can resolve from the target's link graph
#    (`$<TARGET_RUNTIME_DLLS>`; often empty now that SDL3 links statically).
#    On ELF / Mach-O platforms shared libraries resolve through rpath, so this part
#    is a no-op there.
#  * `EXTRA` copies loose files that are NOT link dependencies and so are invisible
#    to TARGET_RUNTIME_DLLS - e.g. a library loaded at runtime via dlopen/LoadLibrary
#    (dxcompiler.dll, wgpu-native), or a known vendored DLL path. Empty entries are
#    skipped, so passing an unset variable is harmless.
#
# copy_if_different keeps the copy incremental; a file staged twice is harmless.
#
# copy_if_different requires >=1 source, but a target may link ZERO runtime DLLs (e.g. since SDL3
# is linked statically, an app with no other shared deps has an empty TARGET_RUNTIME_DLLS). The
# WIN32 copy below switches to a no-op ('cmake -E true') in that case so the POST_BUILD step does
# not fail on a sourceless copy_if_different.
function(util_copy_runtime_deps target)
    cmake_parse_arguments(ARG "" "" "EXTRA" ${ARGN})

    if(WIN32)
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E
                    $<IF:$<BOOL:$<TARGET_RUNTIME_DLLS:${target}>>,copy_if_different,true>
                    $<TARGET_RUNTIME_DLLS:${target}> $<TARGET_FILE_DIR:${target}>
            COMMAND_EXPAND_LISTS VERBATIM
            COMMENT "Staging linked runtime DLLs next to ${target}")
    endif()

    foreach(_f IN LISTS ARG_EXTRA)
        if(_f)
            add_custom_command(TARGET ${target} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different "${_f}" $<TARGET_FILE_DIR:${target}>
                VERBATIM
                COMMENT "Staging ${_f} next to ${target}")
        endif()
    endforeach()

    # Config the build is the source of truth for: the basenames of the runtime libs staged next to
    # <target>, one per line, written to "<target>.runtime-libs". The export host-template reads this
    # for its sidecar list instead of hard-coding it, so it tracks dep changes (a shell swap changes
    # the DLLs -> the list follows). On rpath platforms TARGET_RUNTIME_DLLS is empty, so this lists
    # only EXTRA; on Windows it lists both.
    set(_dr_libs "$<TARGET_RUNTIME_DLLS:${target}>")
    if(ARG_EXTRA)
        list(APPEND _dr_libs ${ARG_EXTRA})   # configure-time paths (e.g. the vendored SDL3 DLL)
    endif()
    file(GENERATE
        OUTPUT  "$<TARGET_FILE_DIR:${target}>/$<TARGET_FILE_BASE_NAME:${target}>.runtime-libs"
        CONTENT "$<JOIN:$<PATH:GET_FILENAME,${_dr_libs}>,\n>\n"
        TARGET  ${target})
endfunction()

# util_add_engine_library(<name> ALIAS <ns::alias>)
#
# The one place first-party library targets are declared (all 179 of them):
# STATIC by default, SHARED when ENGINE_SHARED_LIBS is ON. Declaring through a
# single seam means the shared-build switch, visibility presets, export macros,
# and SOVERSION land here once instead of as a 179-file edit
# (Documentation/Specs/shared-libraries.md, phase P3).
function(util_add_engine_library name)
    cmake_parse_arguments(UAEL "" "ALIAS" "" ${ARGN})
    if(NOT UAEL_ALIAS)
        message(FATAL_ERROR "util_add_engine_library(${name}): ALIAS <ns::name> is required")
    endif()
    if(ENGINE_SHARED_LIBS)
        add_library(${name} SHARED)
        # Inline-function symbols stay DSO-local (smaller dynamic symbol tables,
        # faster loads). Safe: static locals in inline functions are still unified
        # by the linker, and the sharedlib tripwire keeps STATE out of interface
        # inlines anyway. Full hidden visibility + export annotations wait on the
        # MSVC modules+dllexport prototype (shared-libraries.md P5).
        set_target_properties(${name} PROPERTIES VISIBILITY_INLINES_HIDDEN ON)
    else()
        add_library(${name} STATIC)
    endif()
    add_library(${UAEL_ALIAS} ALIAS ${name})
endfunction()
