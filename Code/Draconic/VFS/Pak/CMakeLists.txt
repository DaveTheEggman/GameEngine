# Draconic::VFSPak — `draconic.vfs.pak` (packed read-only archive backend + builder).
# Separate from the core VFS so future compression codecs stay out of it.

add_library(DraconicVFSPak STATIC)
add_library(Draconic::VFSPak ALIAS DraconicVFSPak)

target_link_libraries(DraconicVFSPak PUBLIC Draconic::Core Draconic::VFS Draconic::Policy)

target_sources(DraconicVFSPak
    PUBLIC
        FILE_SET vfs_pak_modules TYPE CXX_MODULES FILES
            PakModule.cppm)

if(DRACONIC_BUILD_TESTS)
    add_subdirectory(Tests)
endif()
