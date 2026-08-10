# Pipeline::Registration - `pipeline.registration`: the pipeline's composition root. Links every
# pipeline module (its whole job - the deliberate fan-in point) and exposes RegisterPipelineTypes /
# RegisterAllBuilders / RegisterAllImporters. Headless (no shell/GPU/editor): the CLI cooker, export
# packager, editor, and MCP server all consume it. NO Editor::Core dependency by design.

add_library(Pipeline.Registration STATIC)
add_library(Pipeline::Registration ALIAS Pipeline.Registration)

target_link_libraries(Pipeline.Registration PUBLIC
    Foundation::Core Foundation::Content Foundation::VFS
    Foundation::Scene Foundation::Scene.Resource
    Pipeline::Core Pipeline::Importer
    Pipeline::Texture Pipeline::Fonts Pipeline::Image Pipeline::Geometry
    Pipeline::Animation Pipeline::Materials Pipeline::Shaders Pipeline::Particles
    Foundation::Input Foundation::Input.Resource Pipeline::Input
    Pipeline::ModelImporter Foundation::Image.Resource
    Foundation::Physics Foundation::Physics.Resource Pipeline::Physics
    Foundation::Audio Foundation::Audio.Resource Pipeline::Audio
    Foundation::Script Foundation::Script.Wren Foundation::Script.AngelScript Foundation::Script.Luau
    Foundation::Script.Resource Foundation::Script.Facades
    Pipeline::Script Pipeline::Script.Wren Pipeline::Script.AngelScript Pipeline::Script.Luau
    Foundation::UI.Resource Pipeline::UI
    Foundation::Policy)

target_sources(Pipeline.Registration
    PRIVATE
        RegistrationImpl.cpp
    PUBLIC
        FILE_SET pipeline_registration_modules TYPE CXX_MODULES FILES
            Registration.cppm)
