# Draconic::XmlSerialization - `draconic.xml.serialization`.
# An XML backend for Core's ISerializer contract. Depends on Core + Xml.

add_library(DraconicXmlSerialization STATIC)
add_library(Draconic::XmlSerialization ALIAS DraconicXmlSerialization)

target_link_libraries(DraconicXmlSerialization PUBLIC Draconic::Core Draconic::Xml Draconic::Policy)

target_sources(DraconicXmlSerialization
    PUBLIC
        FILE_SET xml_serialization_modules TYPE CXX_MODULES FILES
            XmlSerializer.cppm)

if(DRACONIC_BUILD_TESTS)
    add_subdirectory(Tests)
endif()
