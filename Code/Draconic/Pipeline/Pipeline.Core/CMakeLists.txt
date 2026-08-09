# Draconic::Pipeline.Core - `draconic.pipeline.core` (asset-pipeline authoring base: Asset +
# builders + the builder registry). Tooling only - the runtime never links this.

add_library(Draconic.Pipeline.Core STATIC)
add_library(Draconic::Pipeline.Core ALIAS Draconic.Pipeline.Core)

target_link_libraries(Draconic.Pipeline.Core PUBLIC Draconic::Core Draconic::Content Draconic::VFS Draconic::Policy)

target_sources(Draconic.Pipeline.Core
    PRIVATE
        AssetImpl.cpp
    PUBLIC
        FILE_SET pipeline_modules TYPE CXX_MODULES FILES
            Asset.cppm)
