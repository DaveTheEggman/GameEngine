# Raptor::ImageEditor — `raptor.image.editor` (tooling: ImageAsset + builder).
add_library(RaptorImageEditor STATIC)
add_library(Raptor::ImageEditor ALIAS RaptorImageEditor)
target_link_libraries(RaptorImageEditor PUBLIC Raptor::Core Raptor::Editor Raptor::Image Raptor::ImageIO Raptor::ImageResource Raptor::Content Raptor::Policy)
target_sources(RaptorImageEditor PUBLIC FILE_SET image_editor_modules TYPE CXX_MODULES FILES ImageAsset.cppm)
if(RAPTOR_BUILD_TESTS)
    add_subdirectory(Tests)
endif()
