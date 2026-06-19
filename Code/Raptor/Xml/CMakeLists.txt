# Raptor::Xml — `raptor.xml` (DOM XML parser + writer, UTF-8).
# Faithful port of the hand-written Sedulous.Xml library. Depends only on Core.

add_library(RaptorXml STATIC)
add_library(Raptor::Xml ALIAS RaptorXml)

target_link_libraries(RaptorXml PUBLIC Raptor::Core Raptor::Policy)

target_sources(RaptorXml
    PUBLIC
        FILE_SET xml_modules TYPE CXX_MODULES FILES
            Result.cppm
            Lexer.cppm
            Escape.cppm
            Namespace.cppm
            Nodes.cppm
            Writer.cppm
            Document.cppm
            XmlModule.cppm)

if(RAPTOR_BUILD_TESTS)
    add_subdirectory(Tests)
endif()
