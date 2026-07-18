# Draconic::UIResource - `draconic.ui.resource` (cooked UI documents + themes; UI-framework-free).
add_library(DraconicUIResource STATIC)
add_library(Draconic::UIResource ALIAS DraconicUIResource)
target_link_libraries(DraconicUIResource PUBLIC
    Draconic::Core Draconic::Resource Draconic::Content Draconic::Policy)
target_sources(DraconicUIResource PUBLIC FILE_SET ui_resource_modules TYPE CXX_MODULES FILES UIResource.cppm)
