# Pipeline::Core - `draconic.pipeline.core` (asset-pipeline authoring base: Asset +
# builders + the builder registry). Tooling only - the runtime never links this.

add_library(Pipeline.Core STATIC)
add_library(Pipeline::Core ALIAS Pipeline.Core)

target_link_libraries(Pipeline.Core PUBLIC Foundation::Core Foundation::Content Foundation::VFS Foundation::Policy)

target_sources(Pipeline.Core
    PRIVATE
        AssetImpl.cpp
    PUBLIC
        FILE_SET pipeline_modules TYPE CXX_MODULES FILES
            Asset.cppm)
