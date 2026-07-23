# Draconic::Content - `draconic.content` (content database: Groups + Instances of
# serializable objects, addressed by Guid/path, stored over a VFS mount).

add_library(DraconicContent STATIC)
add_library(Draconic::Content ALIAS DraconicContent)

target_link_libraries(DraconicContent PUBLIC Draconic::Core Draconic::VFS Draconic::Policy)

target_sources(DraconicContent
    PRIVATE
        ContentModuleImpl.cpp   # out-of-line defs (see ContentModule.cppm)
    PUBLIC
        FILE_SET content_modules TYPE CXX_MODULES FILES
            ContentModule.cppm)

if(DRACONIC_BUILD_TESTS)
    add_subdirectory(Tests)
endif()
