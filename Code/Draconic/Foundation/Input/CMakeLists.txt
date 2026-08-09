# Foundation::Input - `foundation.input`: the action-mapping layer (data model + ActionRuntime)
# over Core + Shell. Engine-global (per player, not per scene).
add_library(Input STATIC)
add_library(Foundation::Input ALIAS Input)

target_link_libraries(Input
    PUBLIC Foundation::Core Foundation::Shell Foundation::Policy)

target_sources(Input
    PRIVATE
        InputReflectionImpl.cpp   # input leaf-type + enum reflection (GCC hygiene: out of interfaces)
    PUBLIC
        FILE_SET input_modules TYPE CXX_MODULES FILES
            InputModule.cppm
            InputMap.cppm
            ActionRuntime.cppm)

