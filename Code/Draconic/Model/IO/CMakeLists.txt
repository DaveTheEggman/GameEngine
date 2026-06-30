add_library(DraconicModelIO STATIC)
add_library(Draconic::ModelIO ALIAS DraconicModelIO)
target_link_libraries(DraconicModelIO PUBLIC Draconic::Core Draconic::Model Draconic::Policy)
target_sources(DraconicModelIO PUBLIC FILE_SET model_io_modules TYPE CXX_MODULES FILES ModelIOModule.cppm)
