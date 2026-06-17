add_library(RaptorModelIO STATIC)
add_library(Raptor::ModelIO ALIAS RaptorModelIO)
target_link_libraries(RaptorModelIO PUBLIC Raptor::Core Raptor::Model Raptor::Policy)
target_sources(RaptorModelIO PUBLIC FILE_SET model_io_modules TYPE CXX_MODULES FILES ModelIOModule.cppm)
