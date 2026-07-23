# Draconic::InputSubsystem - `draconic.input.subsystem`: runtime tick + device provider seam.
add_library(DraconicInputSubsystem STATIC)
add_library(Draconic::InputSubsystem ALIAS DraconicInputSubsystem)

target_link_libraries(DraconicInputSubsystem
    PUBLIC Draconic::Core Draconic::Shell Draconic::Runtime Draconic::Script Draconic::Input Draconic::Policy)

target_sources(DraconicInputSubsystem
    PRIVATE
        InputSubsystemImpl.cpp   # DRACONIC_REFLECT body (see InputSubsystem.cppm)
    PUBLIC
        FILE_SET input_subsystem_modules TYPE CXX_MODULES FILES
            InputSubsystem.cppm)
