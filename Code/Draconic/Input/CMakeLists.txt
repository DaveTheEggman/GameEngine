# Draconic::Input - `draconic.input`: the action-mapping layer (data model + ActionRuntime)
# over Core + Shell. Engine-global (per player, not per scene).
add_library(DraconicInput STATIC)
add_library(Draconic::Input ALIAS DraconicInput)

target_link_libraries(DraconicInput
    PUBLIC Draconic::Core Draconic::Shell Draconic::Policy)

target_sources(DraconicInput
    PUBLIC
        FILE_SET input_modules TYPE CXX_MODULES FILES
            InputModule.cppm
            InputMap.cppm
            ActionRuntime.cppm)

add_subdirectory(Resource)
if(DRACONIC_BUILD_TOOLS)
    add_subdirectory(Editor)
endif()

if(DRACONIC_BUILD_TESTS)
    add_subdirectory(Tests)
endif()
