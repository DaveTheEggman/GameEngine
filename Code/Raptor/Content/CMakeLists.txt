# Raptor::Content — `raptor.content` (content database: Groups + Instances of
# serializable objects, addressed by Guid/path, stored over a VFS mount).

add_library(RaptorContent STATIC)
add_library(Raptor::Content ALIAS RaptorContent)

target_link_libraries(RaptorContent PUBLIC Raptor::Core Raptor::VFS Raptor::Policy)

target_sources(RaptorContent
    PUBLIC
        FILE_SET content_modules TYPE CXX_MODULES FILES
            ContentModule.cppm)

if(RAPTOR_BUILD_TESTS)
    add_subdirectory(Tests)
endif()
