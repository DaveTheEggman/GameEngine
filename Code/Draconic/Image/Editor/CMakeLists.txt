# Draconic::ImageEditor — `draconic.image.editor` (tooling: ImageAsset + builder).
add_library(DraconicImageEditor STATIC)
add_library(Draconic::ImageEditor ALIAS DraconicImageEditor)
target_link_libraries(DraconicImageEditor PUBLIC Draconic::Core Draconic::Editor Draconic::Image Draconic::ImageIO Draconic::ImageResource Draconic::Content Draconic::Policy)
target_sources(DraconicImageEditor PUBLIC FILE_SET image_editor_modules TYPE CXX_MODULES FILES ImageAsset.cppm)
if(DRACONIC_BUILD_TESTS)
    add_subdirectory(Tests)
endif()
