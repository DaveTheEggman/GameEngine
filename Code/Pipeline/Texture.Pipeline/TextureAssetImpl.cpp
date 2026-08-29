// Pipeline::Texture - reflection implementation unit: TextureAsset's reflected surface.
//
// Kept OUT of the TextureAsset.cppm interface (REFLECT_MEMBERS bodies make GCC emit a gcm
// cluster; see gcc-module-interface-hygiene). The class declares its identity via RTTI_OBJECT
// in the interface; this unit defines TextureAsset::StaticType() WITH properties + tooling
// attributes, so the generic asset page and the script backends see the authored surface.
// The enum property types (TextureShape/Filter/Wrap, ImageColorSpace) are reflected in their
// owning modules and registered by RegisterTextureAsset.

module;
#include "Core/Prelude.h"
#include "Core/Reflection/Reflect.h"

module texture.pipeline;

import foundation.core;
import pipeline.core;
import foundation.texture;
import foundation.image;

using namespace foundation::core;
using namespace foundation::texture;

namespace pipeline{
    REFLECT_MEMBERS(TextureAsset, "rtti::pipeline::texture")
    {
        builder.DataVersion(2) // v2 = asset-variants usage/compression (see Serialize)
            .Attribute("displayName", String(u8"Texture"))
            .Attribute("category", String(u8"Textures"))
            .Property<&TextureAsset::colorSpace>("colorSpace")
            .PropAttribute("displayName", String(u8"Color Space"))
            .Property<&TextureAsset::usage>("usage")
            .PropAttribute("displayName", String(u8"Usage"))
            .PropAttribute("description",
                           String(u8"What the texture is for - selects the block-compression format"))
            .Property<&TextureAsset::compression>("compression")
            .PropAttribute("displayName", String(u8"Compression"))
            .PropAttribute("description",
                           String(u8"Default (policy), None (raw), or Quality (BC7 + max effort)"))
            .Property<&TextureAsset::shape>("shape")
            .PropAttribute("displayName", String(u8"Shape"))
            .Property<&TextureAsset::minFilter>("minFilter")
            .PropAttribute("displayName", String(u8"Min Filter"))
            .Property<&TextureAsset::magFilter>("magFilter")
            .PropAttribute("displayName", String(u8"Mag Filter"))
            .Property<&TextureAsset::wrapU>("wrapU")
            .PropAttribute("displayName", String(u8"Wrap U"))
            .Property<&TextureAsset::wrapV>("wrapV")
            .PropAttribute("displayName", String(u8"Wrap V"))
            .Property<&TextureAsset::wrapW>("wrapW")
            .PropAttribute("displayName", String(u8"Wrap W"))
            .Property<&TextureAsset::generateMipmaps>("generateMipmaps")
            .PropAttribute("displayName", String(u8"Generate Mipmaps"))
            .Property<&TextureAsset::anisotropy>("anisotropy")
            .PropAttribute("displayName", String(u8"Anisotropy"))
            .PropAttribute("range", Float4{1.0f, 16.0f, 1.0f, 0.0f})
            .Property<&TextureAsset::embeddedWidth>("embeddedWidth")
            .PropAttribute("displayName", String(u8"Embedded Width"))
            .PropAttribute("description",
                           String(u8"Model-import embedded pixels (0 for file-backed textures)"))
            .Property<&TextureAsset::embeddedHeight>("embeddedHeight")
            .PropAttribute("displayName", String(u8"Embedded Height"))
            .PropAttribute("description",
                           String(u8"Model-import embedded pixels (0 for file-backed textures)"));
    }
}
