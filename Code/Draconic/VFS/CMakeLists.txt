# Draconic::VFS - `draconic.vfs` (virtual filesystem: byte access by logical path,
# decoupled from the backing store). Depends only on Core (stream primitives).

add_library(DraconicVFS STATIC)
add_library(Draconic::VFS ALIAS DraconicVFS)

target_link_libraries(DraconicVFS PUBLIC Draconic::Core Draconic::Policy)

target_sources(DraconicVFS
    PUBLIC
        FILE_SET vfs_modules TYPE CXX_MODULES FILES
            VfsModule.cppm
            IFileSystem.cppm
            NativeFileSystem.cppm
            VirtualFileSystem.cppm)

add_subdirectory(Pak)   # packed read-only archive backend

if(DRACONIC_BUILD_TESTS)
    add_subdirectory(Tests)
endif()
