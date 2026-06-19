# Raptor::TexturesImporter — `raptor.textures.importer` (image files -> TextureResource).

add_library(RaptorTexturesImporter STATIC)
add_library(Raptor::TexturesImporter ALIAS RaptorTexturesImporter)

target_link_libraries(RaptorTexturesImporter
    PUBLIC Raptor::Core Raptor::Image Raptor::ImageIO Raptor::Textures Raptor::TexturesResource Raptor::Policy)

target_sources(RaptorTexturesImporter
    PUBLIC
        FILE_SET textures_importer_modules TYPE CXX_MODULES FILES
            TextureImporter.cppm)

if(RAPTOR_BUILD_TESTS)
    add_subdirectory(Tests)
endif()
