# Raptor::VFSPak — `raptor.vfs.pak` (packed read-only archive backend + builder).
# Separate from the core VFS so future compression codecs stay out of it.

add_library(RaptorVFSPak STATIC)
add_library(Raptor::VFSPak ALIAS RaptorVFSPak)

target_link_libraries(RaptorVFSPak PUBLIC Raptor::Core Raptor::VFS Raptor::Policy)

target_sources(RaptorVFSPak
    PUBLIC
        FILE_SET vfs_pak_modules TYPE CXX_MODULES FILES
            PakModule.cppm)

if(RAPTOR_BUILD_TESTS)
    add_subdirectory(Tests)
endif()
