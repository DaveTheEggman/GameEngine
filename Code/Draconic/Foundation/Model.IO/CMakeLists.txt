add_library(Model.IO STATIC)
add_library(Foundation::Model.IO ALIAS Model.IO)
target_link_libraries(Model.IO PUBLIC Foundation::Core Foundation::Model Foundation::Policy)
target_sources(Model.IO PUBLIC FILE_SET model_io_modules TYPE CXX_MODULES FILES ModelIOModule.cppm)
