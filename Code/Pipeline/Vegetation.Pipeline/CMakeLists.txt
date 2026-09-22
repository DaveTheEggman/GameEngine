# Pipeline::Vegetation - `vegetation.pipeline`: the VegetationMaskAsset (the painted vegetation
# mask's SOURCE envelope: dims + plane count; the planes ride the "densities" sidecar), its cook
# into the VegetationMask resource, and the PNG importer (one density plane per channel). The
# splatmap pipeline's shape (Pipeline::Terrain).
util_add_engine_library(Vegetation.Pipeline ALIAS Pipeline::Vegetation)
target_link_libraries(Vegetation.Pipeline PUBLIC
    Foundation::Core Foundation::VFS Pipeline::Core Pipeline::Importer
    Foundation::Image Foundation::Image.IO
    Foundation::Vegetation.Resource Foundation::Content Foundation::Policy)
target_sources(Vegetation.Pipeline
    PRIVATE
        VegetationMaskAssetImpl.cpp
    PUBLIC FILE_SET vegetation_pipeline_modules TYPE CXX_MODULES FILES VegetationMaskAsset.cppm)
