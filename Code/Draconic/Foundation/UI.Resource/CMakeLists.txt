# Foundation::UI.Resource - `draconic.ui.resource` (cooked UI documents + themes; UI-framework-free).
add_library(UI.Resource STATIC)
add_library(Foundation::UI.Resource ALIAS UI.Resource)
target_link_libraries(UI.Resource PUBLIC
    Foundation::Core Foundation::Resource Foundation::Content Foundation::Policy)
target_sources(UI.Resource PUBLIC FILE_SET ui_resource_modules TYPE CXX_MODULES FILES UIResource.cppm)
