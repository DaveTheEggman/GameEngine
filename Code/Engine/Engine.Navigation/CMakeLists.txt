# Engine::Navigation - `engine.navigation`: scene integration
# (zone + agent components + per-scene Detour crowd + steering writeback).
add_library(Engine.Navigation STATIC)
add_library(Engine::Navigation ALIAS Engine.Navigation)

target_link_libraries(Engine.Navigation
    PUBLIC Foundation::Core Foundation::Runtime Foundation::Scene Engine::Scene Foundation::Resource
           Foundation::Navigation Foundation::Navigation.Resource
           Foundation::Render Engine::Render
           Foundation::Script.Facades Foundation::Profiler Foundation::Policy)

target_sources(Engine.Navigation
    PRIVATE
        NavigationSubsystemImpl.cpp   # REFLECT_VALUE bodies (kept out of interface units)
    PUBLIC
        FILE_SET navigation_engine_modules TYPE CXX_MODULES FILES
            NavigationModule.cppm
            NavigationComponents.cppm
            NavigationSubsystem.cppm)
