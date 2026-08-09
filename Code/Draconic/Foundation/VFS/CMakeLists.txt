# Foundation::VFS - `draconic.vfs` (virtual filesystem: byte access by logical path,
# decoupled from the backing store). Depends only on Core (stream primitives).

add_library(VFS STATIC)
add_library(Foundation::VFS ALIAS VFS)

target_link_libraries(VFS PUBLIC Foundation::Core Foundation::Policy)

target_sources(VFS
    PRIVATE
        VFSReflectionImpl.cpp
    PUBLIC
        FILE_SET vfs_modules TYPE CXX_MODULES FILES
            VfsModule.cppm
            IFileSystem.cppm
            SourcePath.cppm
            NativeFileSystem.cppm
            VirtualFileSystem.cppm)

