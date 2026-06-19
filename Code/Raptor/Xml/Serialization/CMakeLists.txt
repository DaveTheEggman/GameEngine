# Raptor::XmlSerialization — `raptor.xml.serialization`.
# An XML backend for Core's ISerializer contract. Depends on Core + Xml.

add_library(RaptorXmlSerialization STATIC)
add_library(Raptor::XmlSerialization ALIAS RaptorXmlSerialization)

target_link_libraries(RaptorXmlSerialization PUBLIC Raptor::Core Raptor::Xml Raptor::Policy)

target_sources(RaptorXmlSerialization
    PUBLIC
        FILE_SET xml_serialization_modules TYPE CXX_MODULES FILES
            XmlSerializer.cppm)

if(RAPTOR_BUILD_TESTS)
    add_subdirectory(Tests)
endif()
