# Raptor::RuntimeDefaultApp — `raptor.runtime.defaultapp` (DefaultApplication: the
# opinionated IApplication that registers the standard engine subsystems). Kept in
# its own library so the base client (Raptor::RuntimeClient) never pulls in the
# engine subsystem libraries — only apps that opt into the defaults link this.
# As engine subsystems land, add them as link dependencies HERE.

add_library(RaptorRuntimeDefaultApp STATIC)
add_library(Raptor::RuntimeDefaultApp ALIAS RaptorRuntimeDefaultApp)

target_link_libraries(RaptorRuntimeDefaultApp
    PUBLIC Raptor::Core Raptor::RuntimeClient Raptor::Policy)

target_sources(RaptorRuntimeDefaultApp
    PUBLIC
        FILE_SET runtime_defaultapp_modules TYPE CXX_MODULES FILES
            DefaultApplication.cppm)
