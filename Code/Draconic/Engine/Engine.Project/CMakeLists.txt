# Engine::Project - `engine.project`, the runtime-side project definition (manifest +
# layout constants). Editor-free by design: shipping binaries link this, never the editor.
add_library(Engine.Project STATIC)
add_library(Engine::Project ALIAS Engine.Project)

target_link_libraries(Engine.Project
    PUBLIC Foundation::Core Foundation::VFS Foundation::Xml.Serialization Foundation::Policy)

target_sources(Engine.Project
    PUBLIC
        FILE_SET project_modules TYPE CXX_MODULES FILES
            ProjectModule.cppm)
