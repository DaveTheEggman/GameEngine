add_executable(RaptorSample001Triangle Main.cpp)
target_link_libraries(RaptorSample001Triangle PRIVATE
    Raptor::SamplesFramework Raptor::RHIVulkan Raptor::Core Raptor::Policy)
