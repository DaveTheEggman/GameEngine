# Draconic::Input - `draconic.input`: the action-mapping layer (data model + ActionRuntime)
# over Core + Shell. Engine-global (per player, not per scene).
add_library(Draconic.Input STATIC)
add_library(Draconic::Input ALIAS Draconic.Input)

target_link_libraries(Draconic.Input
    PUBLIC Draconic::Core Draconic::Shell Draconic::Policy)

target_sources(Draconic.Input
    PUBLIC
        FILE_SET input_modules TYPE CXX_MODULES FILES
            InputModule.cppm
            InputMap.cppm
            ActionRuntime.cppm)

add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../Draconic.Input.Resource ${CMAKE_CURRENT_BINARY_DIR}/Resource)
if(DRACONIC_BUILD_TOOLS)
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../../Editor/Draconic.Input.Editor ${CMAKE_CURRENT_BINARY_DIR}/Editor)
endif()

if(DRACONIC_BUILD_TESTS)
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../Draconic.Input.Tests ${CMAKE_CURRENT_BINARY_DIR}/Tests)
endif()
