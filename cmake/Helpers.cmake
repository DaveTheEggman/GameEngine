# Reusable CMake helpers. Included once from the root CMakeLists, so the
# functions defined here are visible in every add_subdirectory() below it.

# Where these helpers live, so a helper can invoke a sibling script by path from a
# custom command (CMAKE_CURRENT_LIST_DIR is the CALLER's directory inside a function).
set(UTIL_CMAKE_DIR "${CMAKE_CURRENT_LIST_DIR}" CACHE INTERNAL "cmake/ helper directory")

# dumpbin, for the MSVC shared-library export .def (see util_add_engine_library). It sits
# beside cl.exe in the toolchain; look there first so we get the toolset's own copy rather
# than whatever a Developer prompt happened to put on PATH. NOTE that MSVC is also true for
# clang-cl, whose CMAKE_CXX_COMPILER lives in LLVM/bin - nowhere near dumpbin - so the PATH
# search find_program does after HINTS is the one that matters there.
if(MSVC AND NOT UTIL_DUMPBIN)
    get_filename_component(_util_cl_dir "${CMAKE_CXX_COMPILER}" DIRECTORY)
    find_program(UTIL_DUMPBIN NAMES dumpbin HINTS "${_util_cl_dir}")
    if(NOT UTIL_DUMPBIN)
        # Only fatal for a SHARED build - a static build never generates a .def. Failing here
        # beats a PRE_LINK step failing identically on all 179 libraries with no explanation.
        if(ENGINE_SHARED_LIBS)
            message(FATAL_ERROR
                "ENGINE_SHARED_LIBS on Windows needs dumpbin.exe to generate export .def files "
                "(cmake/GenerateModuleDef.cmake), and it was not found beside "
                "'${CMAKE_CXX_COMPILER}' or on PATH. Configure from a Developer Command Prompt, "
                "or pass -DUTIL_DUMPBIN=<path to dumpbin.exe>.")
        endif()
        set(UTIL_DUMPBIN "dumpbin" CACHE FILEPATH "dumpbin used to build export .def files")
    endif()
endif()

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
        # MSVC has no default-visibility equivalent: nothing leaves a DLL without an
        # export. WINDOWS_EXPORT_ALL_SYMBOLS is the obvious answer and does NOT work here
        # - it drops every C++20 module-attached symbol (Core exported 221 CRT/STL names
        # and none of its own 1994 module functions). Generate the .def ourselves instead;
        # link.exe accepts the module-decorated names fine. shared-libraries.md P5.
        if(MSVC)
            set(_uael_def "${CMAKE_CURRENT_BINARY_DIR}/${name}_exports.def")
            # The object list goes through a FILE, not the command line: passing
            # $<TARGET_OBJECTS> as a -D argument needs COMMAND_EXPAND_LISTS, which then
            # splits the ;-list into separate argv entries and the script sees only the
            # first object (found 8 symbols instead of 2252 - this exact bug).
            set(_uael_objs "${CMAKE_CURRENT_BINARY_DIR}/${name}_objects.txt")
            file(GENERATE OUTPUT "${_uael_objs}"
                 CONTENT "$<JOIN:$<TARGET_OBJECTS:${name}>,\n>\n" TARGET ${name})
            add_custom_command(
                TARGET ${name} PRE_LINK
                COMMAND ${CMAKE_COMMAND}
                        -DDUMPBIN=${UTIL_DUMPBIN}
                        -DOBJECTS_FILE=${_uael_objs}
                        -DDEF_FILE=${_uael_def}
                        -P "${UTIL_CMAKE_DIR}/GenerateModuleDef.cmake"
                VERBATIM
                COMMENT "Generating module-aware export .def for ${name}")
            # The .def is written by the PRE_LINK step above, so it exists by link time.
            target_link_options(${name} PRIVATE "/DEF:${_uael_def}")
        endif()
    else()
        add_library(${name} STATIC)
    endif()
    add_library(${UAEL_ALIAS} ALIAS ${name})
endfunction()
