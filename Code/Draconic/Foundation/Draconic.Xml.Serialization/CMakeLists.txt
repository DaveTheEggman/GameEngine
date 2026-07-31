# Draconic::XmlSerialization - `draconic.xml.serialization`.
# An XML backend for Core's ISerializer contract. Depends on Core + Xml.

add_library(Draconic.Xml.Serialization STATIC)
add_library(Draconic::XmlSerialization ALIAS Draconic.Xml.Serialization)

target_link_libraries(Draconic.Xml.Serialization PUBLIC Draconic::Core Draconic::Xml Draconic::Policy)

target_sources(Draconic.Xml.Serialization
    PUBLIC
        FILE_SET xml_serialization_modules TYPE CXX_MODULES FILES
            XmlSerializer.cppm)

if(DRACONIC_BUILD_TESTS)
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../Draconic.Xml.Serialization.Tests ${CMAKE_CURRENT_BINARY_DIR}/Tests)
endif()
