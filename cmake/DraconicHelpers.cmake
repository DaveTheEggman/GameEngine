# Reusable Draconic CMake helpers. Included once from the root CMakeLists, so the
# functions defined here are visible in every add_subdirectory() below it.

# draconic_copy_runtime_deps(<target> [EXTRA <file>...])
#
# POST_BUILD, stage everything <target> needs to run next to its executable:
#
#  * On Windows, every runtime DLL CMake can resolve from the target's link graph
#    (`$<TARGET_RUNTIME_DLLS>` - e.g. SDL3.dll via the imported SDL3::SDL3 target).
#    On ELF / Mach-O platforms shared libraries resolve through rpath, so this part
#    is a no-op there.
#  * `EXTRA` copies loose files that are NOT link dependencies and so are invisible
#    to TARGET_RUNTIME_DLLS - e.g. a library loaded at runtime via dlopen/LoadLibrary
#    (dxcompiler.dll), or a known vendored DLL path. Empty entries are skipped, so
#    passing an unset variable (e.g. ${DRACONIC_SDL3_DLL} on Linux) is harmless.
#
# copy_if_different keeps the copy incremental; a file staged twice is harmless.
#
# NOTE: on Windows a bare call (no EXTRA) needs the target to link >=1 shared
# dependency - copy_if_different requires at least one source. All our apps link
# SDL3, so in practice pass `EXTRA ${DRACONIC_SDL3_DLL}` and it is always covered.
function(draconic_copy_runtime_deps target)
    cmake_parse_arguments(ARG "" "" "EXTRA" ${ARGN})

    if(WIN32)
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
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
    # only EXTRA; on Windows it lists both. See docs/design/export.md.
    set(_dr_libs "$<TARGET_RUNTIME_DLLS:${target}>")
    if(ARG_EXTRA)
        list(APPEND _dr_libs ${ARG_EXTRA})   # configure-time paths (e.g. the vendored SDL3 DLL)
    endif()
    file(GENERATE
        OUTPUT  "$<TARGET_FILE_DIR:${target}>/${target}.runtime-libs"
        CONTENT "$<JOIN:$<PATH:GET_FILENAME,${_dr_libs}>,\n>\n"
        TARGET  ${target})
endfunction()
