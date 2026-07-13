# Draconic::Project - `draconic.project`, the runtime-side project definition (manifest +
# layout constants). Editor-free by design: shipping binaries link this, never the editor.
add_library(DraconicProject STATIC)
add_library(Draconic::Project ALIAS DraconicProject)

target_link_libraries(DraconicProject
    PUBLIC Draconic::Core Draconic::VFS Draconic::XmlSerialization Draconic::Policy)

target_sources(DraconicProject
    PUBLIC
        FILE_SET project_modules TYPE CXX_MODULES FILES
            ProjectModule.cppm)
