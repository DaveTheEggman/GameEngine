# Foundation::UI.Resource - `foundation.ui.resource` (cooked UI documents + themes; UI-framework-free).
util_add_engine_library(UI.Resource ALIAS Foundation::UI.Resource)
target_link_libraries(UI.Resource PUBLIC
    Foundation::Core Foundation::Resource Foundation::Content Foundation::Policy)
target_sources(UI.Resource PUBLIC FILE_SET ui_resource_modules TYPE CXX_MODULES FILES UIResource.cppm)
