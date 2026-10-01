# Pipeline::Scene - `scene.pipeline`: the scene domain's authoring side, its New Asset creators
# (a scene seeded with a sun, a prefab with one root entity). The sun needs the render engine's
# light component in a plain scene, not a device, which is why the scene domain has a pipeline
# library of its own linking Engine::Render. Tooling only; the engine never links a pipeline
# library.
util_add_engine_library(Scene.Pipeline ALIAS Pipeline::Scene)
target_link_libraries(Scene.Pipeline PUBLIC
    Foundation::Core Foundation::Content Foundation::Scene Foundation::Scene.Resource
    Engine::Render Pipeline::Core Foundation::Policy)

target_sources(Scene.Pipeline
    PRIVATE
        SceneCreatorsImpl.cpp
    PUBLIC
        FILE_SET scene_pipeline_modules TYPE CXX_MODULES FILES
            SceneCreators.cppm)
