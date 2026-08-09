// ImageEditorPage tests (headless): factory type-dispatch + the asset blob round-trip the
// page's undo snapshots ride. The preview + grid need a live harness (editor app).

#include <doctest/doctest.h>

#include "Core/Prelude.h"

import draconic.core;
import draconic.image;
import draconic.image.pipeline;
import draconic.vfs;
import draconic.editor.image;
import draconic.editor.core;

using namespace foundation::core;
namespace image = foundation::image;

TEST_CASE("ImageEditorPageFactory reports the ImageAsset primary type")
{
    editor::ImageEditorPageFactory factory;
    CHECK(factory.PrimaryType() == &pipeline::ImageAsset::StaticType());
}

TEST_CASE("ImageEditor registers a factory the registry routes for ImageAsset")
{
    editor::EditorContext context;
    editor::RegisterImageEditor(context);

    editor::IEditorPageFactory* found =
        context.Pages().FindFactory(pipeline::ImageAsset::StaticType());
    REQUIRE(found != nullptr);
    CHECK(found->PrimaryType() == &pipeline::ImageAsset::StaticType());
}

TEST_CASE("ImageAsset blob snapshot round-trips fileName + color space (undo path)")
{
    pipeline::ImageAsset a;
    a.fileName = foundation::vfs::SourcePath(u8"Textures/rock.png");
    a.colorSpace = image::ImageColorSpace::Linear;

    MemoryStream stream;
    {
        BinarySerializer ar(stream, SerializeMode::Write);
        a.Serialize(ar);
        REQUIRE(ar.IsOk());
    }
    (void)stream.Seek(0, SeekOrigin::Begin);
    pipeline::ImageAsset b;
    {
        BinarySerializer ar(stream, SerializeMode::Read);
        b.Serialize(ar);
        REQUIRE(ar.IsOk());
    }
    CHECK(b.fileName.View() == u8"Textures/rock.png");
    CHECK(b.colorSpace == image::ImageColorSpace::Linear);
}
