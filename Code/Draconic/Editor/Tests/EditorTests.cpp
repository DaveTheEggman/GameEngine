// Basic checks for the asset-pipeline base (Asset serialize round-trip + builder dispatch shape).
#include <doctest/doctest.h>
#include "Core/Prelude.h"
#include "Core/Reflection/Reflect.h"
import draconic.core;
import draconic.content;
import draconic.editor;
using namespace draconic::core;
using namespace draconic::editor;

namespace
{
    // A concrete asset: source file + one setting.
    class WidgetAsset final : public Asset
    {
        DRACONIC_OBJECT(WidgetAsset, Asset)
    public:
        i32 quality = 0;
        void Serialize(ISerializer& ar) override
        {
            Asset::Serialize(ar);                       // fileName
            draconic::core::Serialize(ar, "quality", quality);
        }
    };
}
DRACONIC_DEFINE_OBJECT(WidgetAsset, "draconic::editor::test")

TEST_CASE("editor: Asset carries a source file path + settings (round-trips)")
{
    WidgetAsset a;
    a.fileName = u8"art/widget.png";
    a.quality = 7;

    MemoryStream buffer;
    { BinarySerializer w(buffer, SerializeMode::Write); a.Serialize(w); REQUIRE(w.IsOk()); }
    REQUIRE(buffer.Seek(0, SeekOrigin::Begin) == 0);

    WidgetAsset b;
    { BinarySerializer r(buffer, SerializeMode::Read); b.Serialize(r); REQUIRE(r.IsOk()); }
    CHECK(b.fileName == StringView(u8"art/widget.png"));
    CHECK(b.quality == 7);
}

TEST_CASE("editor: DefaultAssetBuilder resolves source paths against the asset root")
{
    AssetBuildContext ctx{ u8"assets", nullptr };
    CHECK(DefaultAssetBuilder::ResolveSource(ctx, u8"tex/a.png") == StringView(u8"assets/tex/a.png"));
    AssetBuildContext rootless{ u8"", nullptr };
    CHECK(DefaultAssetBuilder::ResolveSource(rootless, u8"tex/a.png") == StringView(u8"tex/a.png"));
}
