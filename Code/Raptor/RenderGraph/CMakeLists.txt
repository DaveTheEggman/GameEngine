# Raptor::RenderGraph — `raptor.rendergraph` (render graph over the RHI:
# pass dependency resolution, transient resource aliasing, automatic barriers).
# Ported from Sedulous.RenderGraph.

add_library(RaptorRenderGraph STATIC)
add_library(Raptor::RenderGraph ALIAS RaptorRenderGraph)

target_link_libraries(RaptorRenderGraph PUBLIC Raptor::Core Raptor::RHI Raptor::Policy)

target_sources(RaptorRenderGraph
    PUBLIC
        FILE_SET render_graph_modules TYPE CXX_MODULES FILES
            Types.cppm
            Callbacks.cppm
            Descriptors.cppm
            RenderGraphModule.cppm)

if(RAPTOR_BUILD_TESTS)
    add_subdirectory(Tests)
endif()
