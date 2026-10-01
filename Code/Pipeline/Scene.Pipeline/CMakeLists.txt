# Pipeline::Scene - `scene.pipeline`: the scene domain's authoring side, its New Asset creators
# (a scene seeded with a sun, a prefab with one root entity) and a model import's prefab and
# scene (ModelPrefab). Both build components into plain scenes, not devices: the sun is the render
# engine's light, a model's prefab carries mesh, animator and collider components, which is why
# the scene domain has a pipeline library of its own linking those engine domains. Tooling only;
# the engine never links a pipeline library.
util_add_engine_library(Scene.Pipeline ALIAS Pipeline::Scene)
target_link_libraries(Scene.Pipeline PUBLIC
    Foundation::Core Foundation::Content Foundation::Scene Foundation::Scene.Resource
    Engine::Render Engine::Animation Foundation::Physics Engine::Physics Foundation::Materials
    Foundation::Resource Pipeline::Core Pipeline::Importer Pipeline::ModelImporter Foundation::Policy)

target_sources(Scene.Pipeline
    PRIVATE
        SceneCreatorsImpl.cpp
    PUBLIC
        FILE_SET scene_pipeline_modules TYPE CXX_MODULES FILES
            ModelPrefab.cppm
            SceneCreators.cppm)
