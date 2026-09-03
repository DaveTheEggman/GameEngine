// SPDX-License-Identifier: MIT
// Copyright (c) 2026-Present Robert Campbell

// Pipeline::Core - the `editor.core` module.
//
// The tooling/authoring base for the asset pipeline: an `Asset`
// is the editor/source object (references an external source file + import settings) and an
// `IAssetBuilder` cooks it into a runtime *resource* written to the output content database.
// The runtime never links this module - it loads only cooked resources. (Asset = source/editor;
// Resource = runtime/cooked.)
//
// Builder API:
//   - source files are read through the VFS (`AssetBuildContext::sources` mount), never raw
//     paths - builders use ReadSourceBytes/ReadSourceText;
//   - `Version()`: bump when cook logic changes so exactly this builder's products re-cook
//     (folded into the recipe hash; forgetting the bump is the known
//     failure mode, Build > Rebuild All is the big hammer);
//   - `ScanDependencies()`: declares what a build consumes beyond the implicit Asset::fileName -
//     extra FILES, content READS of other instances (hash-chained: editing them re-cooks this),
//     and runtime REFERENCES (existence-only: never re-cook on content change);
//   - `BuilderRegistry`: asset type -> builder routing for the cook driver.

module;
#include "Core/Prelude.h"
#include "Core/Reflection/Reflect.h"

export module pipeline.core;

import foundation.core;
import foundation.content;
export import foundation.vfs; // Asset::fileName is vfs::SourcePath

using namespace foundation::core;

export namespace pipeline
{
    // The availability domain for the Pipeline collection's authoring types (asset classes +
    // import settings). Answers "which processes have this type": the player has them NOT (it
    // loads cooked products, never authoring assets), so Runtime would be wrong; the headless
    // cook/CLI/MCP hosts DO have them without the editor, so Editor is wrong too. The truth table:
    // player = Runtime only; pipeline/CLI/MCP = Runtime + Pipeline; editor = all three. Adding a
    // domain is free - the set is open (foundation.core owns only the default Runtime domain).
    inline constexpr TypeDomain kPipelineTypeDomain{StringView(u8"Pipeline")};

    // Source/authoring asset: a serializable that references an external source
    // file (relative to the sources mount). Concrete assets derive this and
    // add their import settings; call Asset::Serialize for the file name.
    class Asset : public ISerializable
    {
        RTTI_OBJECT(Asset, ISerializable)
    public:
        // Source file, relative to the sources mount (empty = embedded data). Typed:
        // normalization guarantees forward-slash relative form in cooked data - a
        // Windows-authored backslash path heals on load instead of breaking the VFS.
        foundation::vfs::SourcePath fileName;

        void Serialize(ISerializer& ar) override
        {
            foundation::core::Serialize(ar, "fileName", fileName);
        }
    };

    // The export target a cook is producing for. `id` names the target - it is
    // BOTH the per-target cooked-DB directory name AND the recipe platform salt (so a variant
    // product recooks when the target changes). The capability booleans describe the compressed-
    // texture families the target's devices support; a variant builder maps them to its encoder
    // profile (never keys on the id/platform directly - the capability-not-platform rule). The
    // HostTarget() is the always-warm desktop view the editor cooks against.
    struct CookTarget
    {
        String id;         // "host", "web-bc", "web-astc", ... (DB dir + recipe salt)
        bool bc = true;    // BC1-BC7 (desktop + desktop browsers)
        bool astc = false; // ASTC (mobile browsers)
        bool etc2 = false; // ETC2 (not supported)
    };

    // The default always-warm target the editor dev loop cooks against (BC-capable desktop).
    [[nodiscard]] inline CookTarget HostTarget()
    {
        return CookTarget{String(u8"host"), true, false, false};
    }

    // Resolve a target id to its capability profile. The web export splits into
    // two variant targets: "web-bc" (desktop browsers) and "web-astc" (mobile browsers). Everything
    // else - "host", desktop platforms, unknown ids - is BC-capable desktop. New targets
    // (consoles, ETC2 budget mobiles) extend this; the resolver keys on capability, never on platform.
    [[nodiscard]] inline CookTarget CookTargetFor(StringView id)
    {
        if (id == StringView(u8"web-astc"))
        {
            return CookTarget{String(id), /*bc*/ false, /*astc*/ true, /*etc2*/ false};
        }
        return CookTarget{String(id), /*bc*/ true, /*astc*/ false, /*etc2*/ false};
    }

    // Inputs a builder cooks against: the sources mount (all file access through the VFS), the
    // output instance to write the cooked resource into, and the content DB (so a builder can
    // resolve cross-asset references during the bake).
    struct AssetBuildContext
    {
        // The cook's allocator (required - the driver / editor cook service decides):
        // everything a Build() allocates for its products comes from here.
        explicit AssetBuildContext(IAllocator& alloc) noexcept : allocator(&alloc) {}

        IAllocator* allocator;
        foundation::vfs::IFileSystem* sources = nullptr; // mount for Asset::fileName + extra files
        foundation::content::Instance* source =
            nullptr; // the SOURCE instance being cooked (embedded data streams)
        foundation::content::Instance* output = nullptr;     // cooked resource is written here
        foundation::content::IContentDatabase* db = nullptr; // for resolving referenced assets
        // The SOURCE database (asset envelopes + raw sidecars). At cook time `db` is the
        // COOKED-products view, so a builder that needs another asset's SOURCE form (its
        // envelope's fileName, embedded pixels, ...) must read through THIS - Cast'ing a cooked
        // product to its Asset envelope fails (the terrain palette-cook lesson).
        foundation::content::IContentDatabase* sourceDb = nullptr;
        // The export target being produced. Null = the host target (a variant
        // builder falls back to the desktop/BC profile).
        const CookTarget* target = nullptr;
    };

    // What one build consumes beyond the implicit Asset::fileName. The cook driver hashes files
    // and chains `reads`; `references` only order the cook.
    struct AssetDependencies
    {
        Array<foundation::vfs::SourcePath> files; // extra source files read (mount-relative)
        Array<String> sourceStreams; // the source instance's data streams the build reads
                                     // (embedded payloads live in SIDECAR files the envelope
                                     // hash doesn't cover - declaring them chains their bytes)
        Array<Guid> reads;           // instances whose CONTENT this build consumes (hash-chained)
        Array<Guid> references;      // instances the product refers to at runtime (existence only)
    };

    // Whether a builder's output can differ per export target (asset-variants Decision 3). The cook
    // treats INVARIANT products as platform-agnostic: they are cooked ONCE into the host DB and
    // copied forward into every target DB (never recooked), and their recipe carries no platform
    // salt. VARIANT builders (textures: BC vs ASTC) cook per target and their recipe is salted by the
    // target, so the same source produces a distinct product per target. Default INVARIANT so every
    // existing builder keeps today's behavior.
    enum class BuildVariance : u8
    {
        PlatformInvariant,
        PlatformVariant,
    };

    // Cooks one source asset type into a runtime resource (source -> product).
    // Runs in tooling only; writes to ctx.output (WriteObject + WriteData).
    class IAssetBuilder
    {
    public:
        virtual ~IAssetBuilder() = default;

        // The source Asset type this builder handles.
        [[nodiscard]] virtual const TypeInfo* AssetType() const = 0;

        // The cooked resource type this builder writes (the cook driver stamps output
        // instances with it).
        [[nodiscard]] virtual const TypeInfo* ProductType() const = 0;

        // Cook-logic version: BUMP whenever Build()'s output changes for the same inputs.
        [[nodiscard]] virtual u32 Version() const { return 1; }

        // Does this builder's product vary per export target? Default INVARIANT (see BuildVariance).
        [[nodiscard]] virtual BuildVariance Variance() const { return BuildVariance::PlatformInvariant; }

        // Declare extra dependencies (Asset::fileName is implicit). Default: none.
        virtual void ScanDependencies(const Asset& asset, AssetBuildContext& ctx,
                                      AssetDependencies& out)
        {
            (void)asset;
            (void)ctx;
            (void)out;
        }

        // Cook `asset` into ctx.output. Returns Ok or a failure status.
        [[nodiscard]] virtual Status Build(const Asset& asset, AssetBuildContext& ctx) = 0;
    };

    // Convenience base: shared helpers for concrete builders.
    class DefaultAssetBuilder : public IAssetBuilder
    {
    public:
        // Read a whole source file (mount-relative) through the VFS.
        [[nodiscard]] static Result<Array<byte>> ReadSourceBytes(const AssetBuildContext& ctx,
                                                                 StringView fileName)
        {
            if (ctx.sources == nullptr)
            {
                return Err(ErrorCode::InvalidArgument);
            }
            UniquePtr<IStream> stream = ctx.sources->Open(fileName, FileMode::Read);
            if (stream.Get() == nullptr)
            {
                return Err(ErrorCode::NotFound);
            }

            const i64 size = stream->Size();
            if (size < 0)
            {
                return Err(ErrorCode::Unknown);
            }
            Array<byte> buf;
            buf.Resize(static_cast<usize>(size));
            if (size > 0 &&
                stream->Read(buf.Data(), static_cast<u64>(size)) != static_cast<u64>(size))
            {
                return Err(ErrorCode::Unknown);
            }
            return buf;
        }

        // Read a whole source text file (mount-relative) through the VFS.
        [[nodiscard]] static Status ReadSourceText(const AssetBuildContext& ctx,
                                                   StringView fileName, String& out)
        {
            Result<Array<byte>> bytes = ReadSourceBytes(ctx, fileName);
            if (!bytes.HasValue())
            {
                return Status{bytes.Error()};
            }
            out = String(StringView(reinterpret_cast<const utf8char*>(bytes.Value().Data()),
                                    bytes.Value().Size()));
            return Status{};
        }
    };

    // Asset type -> builder routing for the cook driver. Modules register their builders here
    // (the executable assembles the set, mirroring the page-factory/creator registries).
    class BuilderRegistry
    {
    public:
        // The allocator (required - the owner decides) backs registered builders.
        explicit BuilderRegistry(IAllocator& allocator) noexcept : m_allocator(&allocator) {}

        [[nodiscard]] IAllocator& Allocator() const noexcept { return *m_allocator; }

        void Register(UniquePtr<IAssetBuilder> builder)
        {
            if (builder)
            {
                m_builders.PushBack(Move(builder));
            }
        }

        [[nodiscard]] IAssetBuilder* Find(const TypeInfo* assetType) const
        {
            for (const UniquePtr<IAssetBuilder>& b : m_builders)
            {
                if (b->AssetType() == assetType)
                {
                    return b.Get();
                }
            }
            return nullptr;
        }

        [[nodiscard]] IAssetBuilder* FindByTypeName(StringView typeName) const
        {
            for (const UniquePtr<IAssetBuilder>& b : m_builders)
            {
                const TypeInfo* type = b->AssetType();
                if (type != nullptr && type->name != nullptr &&
                    StringView(reinterpret_cast<const utf8char*>(type->name)) == typeName)
                {
                    return b.Get();
                }
            }
            return nullptr;
        }

        [[nodiscard]] usize Count() const noexcept { return m_builders.Size(); }

        /// Visit every registered builder (the registration tripwires audit the whole set -
        /// e.g. "every ProductType() is a registered serializable").
        template <typename F> void ForEach(F&& fn) const
        {
            for (const UniquePtr<IAssetBuilder>& b : m_builders)
            {
                fn(*b);
            }
        }

    private:
        IAllocator* m_allocator;
        Array<UniquePtr<IAssetBuilder>> m_builders;
    };

    // Reflects the Asset base (its fileName property) + SourcePath, so EVERY concrete asset
    // surfaces its source file through the base chain (FindProperty walks bases). Idempotent.
    // Asset::StaticType() itself is defined WITH the fileName property in AssetImpl.cpp.
    void RegisterAssetReflection();
}
