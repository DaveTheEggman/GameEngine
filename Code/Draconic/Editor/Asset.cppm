// Draconic::Editor - the `draconic.editor` module.
//
// The tooling/authoring base for the asset pipeline (Traktor-style): an `Asset`
// is the editor/source object (references an external source file + import
// settings) and an `IAssetBuilder` cooks it into a runtime *resource* written to
// the output content database. The runtime never links this module - it loads
// only cooked resources. (Asset = source/editor; Resource = runtime/cooked.)

module;
#include "Core/Prelude.h"
#include "Core/Reflection/Reflect.h"

export module draconic.editor;

import draconic.core;
import draconic.content;

using namespace draconic::core;

export namespace draconic::editor
{
    // Source/authoring asset: a serializable that references an external source
    // file (relative to the build's asset root). Concrete assets derive this and
    // add their import settings; call Asset::Serialize for the file name.
    class Asset : public ISerializable
    {
        DRACONIC_OBJECT(Asset, ISerializable)
    public:
        String fileName; // source file, relative to the asset root

        void Serialize(ISerializer& ar) override
        {
            draconic::core::Serialize(ar, "fileName", fileName);
        }
    };

    // Inputs a builder cooks against: where source files live + the output
    // instance to write the cooked resource (object + data streams) into.
    struct AssetBuildContext
    {
        StringView assetRoot;                  // base dir for resolving Asset::fileName
        draconic::content::Instance* output;     // cooked resource is written here
    };

    // Cooks one source asset type into a runtime resource (source -> product).
    // Runs in tooling only; writes to ctx.output (WriteObject + WriteData).
    class IAssetBuilder
    {
    public:
        virtual ~IAssetBuilder() = default;

        // The source Asset type this builder handles.
        [[nodiscard]] virtual const TypeInfo* AssetType() const = 0;

        // Cook `asset` into ctx.output. Returns Ok or a failure status.
        [[nodiscard]] virtual Status Build(const Asset& asset, AssetBuildContext& ctx) = 0;
    };

    // Convenience base: shared helpers for concrete builders.
    class DefaultAssetBuilder : public IAssetBuilder
    {
    public:
        // Resolve a source file path against the build's asset root.
        [[nodiscard]] static String ResolveSource(const AssetBuildContext& ctx, StringView fileName)
        {
            return ctx.assetRoot.IsEmpty() ? String(fileName) : PathJoin(ctx.assetRoot, fileName);
        }
    };

    DRACONIC_DEFINE_OBJECT(Asset, "draconic::editor")
}
