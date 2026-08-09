# Engine::Input - `engine.input`: runtime tick + device provider seam.
add_library(Engine.Input STATIC)
add_library(Engine::Input ALIAS Engine.Input)

target_link_libraries(Engine.Input
    PUBLIC Foundation::Core Foundation::Shell Foundation::Runtime Foundation::Script Foundation::Script.Facades Foundation::Input Foundation::Policy)

target_sources(Engine.Input
    PRIVATE
        InputSubsystemImpl.cpp   # REFLECT_MEMBERS body (see InputSubsystem.cppm)
    PUBLIC
        FILE_SET input_subsystem_modules TYPE CXX_MODULES FILES
            InputSubsystem.cppm)
