# Draconic::Xml - `draconic.xml` (DOM XML parser + writer, UTF-8).
# Faithful port of the hand-written Sedulous.Xml library. Depends only on Core.

add_library(DraconicXml STATIC)
add_library(Draconic::Xml ALIAS DraconicXml)

target_link_libraries(DraconicXml PUBLIC Draconic::Core Draconic::Policy)

target_sources(DraconicXml
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

add_subdirectory(Serialization)  # draconic.xml.serialization - XML serializer backend

if(DRACONIC_BUILD_TESTS)
    add_subdirectory(Tests)
endif()
