# Raptor::VFS — `raptor.vfs` (virtual filesystem: byte access by logical path,
# decoupled from the backing store). Depends only on Core (stream primitives).

add_library(RaptorVFS STATIC)
add_library(Raptor::VFS ALIAS RaptorVFS)

target_link_libraries(RaptorVFS PUBLIC Raptor::Core Raptor::Policy)

target_sources(RaptorVFS
    PUBLIC
        FILE_SET vfs_modules TYPE CXX_MODULES FILES
            VfsModule.cppm
            IFileSystem.cppm
            NativeFileSystem.cppm
            VirtualFileSystem.cppm)

add_subdirectory(Pak)   # packed read-only archive backend

if(RAPTOR_BUILD_TESTS)
    add_subdirectory(Tests)
endif()
