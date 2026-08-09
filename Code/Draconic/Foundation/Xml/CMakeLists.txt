# Foundation::Xml - `foundation.xml` (DOM XML parser + writer, UTF-8).
# Faithful port of the hand-written Sedulous.Xml library. Depends only on Core.

add_library(Xml STATIC)
add_library(Foundation::Xml ALIAS Xml)

target_link_libraries(Xml PUBLIC Foundation::Core Foundation::Policy)

target_sources(Xml
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

