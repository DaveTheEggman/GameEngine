// Raptor::ImageEditor — the `raptor.image.editor` module.
//
// Tooling: the source ImageAsset (an image file + color-space intent) and the
// builder that cooks it into a runtime ImageResource (decode the file, write the
// header + "pixels" stream into the output DB). Never linked by the runtime.

module;
#include "Core/Prelude.h"
#include "Core/Reflection/Reflect.h"

export module raptor.image.editor;

import raptor.core;
import raptor.editor;
import raptor.image;
import raptor.image.io;
import raptor.image.resource;
import raptor.content;

using namespace raptor::core;

export namespace raptor::image
{
    // Source asset: references an image file; colorSpace says how to interpret it.
    class ImageAsset final : public raptor::editor::Asset
    {
        RAPTOR_OBJECT(ImageAsset, raptor::editor::Asset)
    public:
        ImageColorSpace colorSpace = ImageColorSpace::Srgb;

        void Serialize(ISerializer& ar) override
        {
            raptor::editor::Asset::Serialize(ar); // fileName
            raptor::core::Serialize(ar, "colorSpace", colorSpace);
        }
    };

    // Cooks an ImageAsset -> ImageResource (decode file -> header + pixel stream).
    class ImageAssetBuilder final : public raptor::editor::DefaultAssetBuilder
    {
    public:
        [[nodiscard]] const TypeInfo* AssetType() const override { return &ImageAsset::StaticType(); }

        [[nodiscard]] Status Build(const raptor::editor::Asset& asset, raptor::editor::AssetBuildContext& ctx) override
        {
            const ImageAsset& ia = static_cast<const ImageAsset&>(asset); // guarded by AssetType()
            if (ctx.output == nullptr) { return Status{ ErrorCode::InvalidArgument }; }

            const String path = ResolveSource(ctx, ia.fileName);
            Image image;
            const Status loaded = io::LoadImage(path, image);
            if (!loaded.IsOk()) { return loaded; }

            ImageResource resource;
            resource.width = image.Width();
            resource.height = image.Height();
            resource.format = image.Format();
            resource.colorSpace = ia.colorSpace;

            const Status wrote = ctx.output->WriteObject(resource);
            if (!wrote.IsOk()) { return wrote; }

            const Span<const u8> px = image.PixelData();
            return ctx.output->WriteData(u8"pixels",
                Span<const byte>(reinterpret_cast<const byte*>(px.Data()), px.Size()));
        }
    };

    // Registers ImageAsset for content-DB construction + deserialization.
    inline void RegisterImageAsset()
    {
        GlobalTypeRegistry().Register(ImageAsset::StaticType());
        RegisterSerializable<ImageAsset>();
    }

    RAPTOR_DEFINE_OBJECT(ImageAsset, "raptor::image")
}
