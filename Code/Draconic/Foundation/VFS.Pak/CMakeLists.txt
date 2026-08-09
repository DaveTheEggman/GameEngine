# Foundation::VFS.Pak - `draconic.vfs.pak` (packed read-only archive backend + builder).
# Separate from the core VFS so future compression codecs stay out of it.

add_library(VFS.Pak STATIC)
add_library(Foundation::VFS.Pak ALIAS VFS.Pak)

target_link_libraries(VFS.Pak PUBLIC Foundation::Core Foundation::VFS Foundation::Policy)

target_sources(VFS.Pak
    PUBLIC
        FILE_SET vfs_pak_modules TYPE CXX_MODULES FILES
            PakModule.cppm)

