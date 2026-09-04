util_add_engine_library(Model.IO ALIAS Foundation::Model.IO)
target_link_libraries(Model.IO PUBLIC Foundation::Core Foundation::Model Foundation::Policy)
target_sources(Model.IO PUBLIC FILE_SET model_io_modules TYPE CXX_MODULES FILES ModelIOModule.cppm)
# Module implementation unit: the loader registry storage (one per process).
target_sources(Model.IO PRIVATE ModelIOImpl.cpp)
