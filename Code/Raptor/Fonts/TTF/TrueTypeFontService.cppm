// Raptor::FontsTTF — raptor.fonts.ttf:service partition
//
// IFontService that loads TrueType/OpenType fonts through the source-format
// pipeline (parse -> bake -> expand-to-RGBA8). With a VFS file system set, the
// locator is a path opened through it; otherwise it is a disk path (for
// sandboxes/tools/tests). Ported from Sedulous.Fonts.TTF/TrueTypeFontService.bf;
// Beef's "Family@Height" string keys become explicit family + size fields.

module;
#include "Core/Prelude.h"

export module raptor.fonts.ttf:service;

import raptor.core;
import raptor.fonts;
import raptor.fonts.io;
import raptor.image;
import raptor.vfs;
import :text_shaper;
import :init;

using namespace raptor::core;

export namespace raptor::fonts
{
    class TrueTypeFontService final : public IFontService
    {
    public:
        // `fileSystem` is optional + non-owning. When set, LoadFont treats the
        // locator as a path opened through it; otherwise as a disk path.
        explicit TrueTypeFontService(raptor::vfs::IFileSystem* fileSystem = nullptr)
            : m_fileSystem(fileSystem)
        {
            TrueTypeFonts::Initialize();
        }

        ~TrueTypeFontService() override
        {
            for (FontEntry* entry : m_fonts)
                DeleteEntry(entry);
            m_fonts.Clear();
        }

        TrueTypeFontService(const TrueTypeFontService&) = delete;
        TrueTypeFontService& operator=(const TrueTypeFontService&) = delete;

        // Load a font from `locator` and build its atlas texture. The first
        // font loaded becomes the default. Returns Success or a failure.
        [[nodiscard]] FontLoadResult LoadFont(WideStringView familyName, StringView locator,
                                              FontLoadOptions options = FontLoadOptions::ExtendedLatin())
        {
            IFont* font = nullptr;
            if (m_fileSystem != nullptr)
            {
                UniquePtr<IStream> stream = m_fileSystem->Open(locator, FileMode::Read);
                if (!stream || !stream->IsValid())
                    return FontLoadResult::FileNotFound;

                const StringView ext = PathExtension(locator);
                Result<IFont*, FontLoadResult> parsed = FontParserFactory::ParseFromStream(*stream, ext, options);
                if (!parsed.HasValue())
                    return parsed.Error();
                font = parsed.Value();
            }
            else
            {
                Result<IFont*, FontLoadResult> parsed = FontParserFactory::ParseFromFile(locator, options);
                if (!parsed.HasValue())
                    return parsed.Error();
                font = parsed.Value();
            }

            return CacheFont(familyName, font, options);
        }

        // Change the default family used by GetFont(pixelHeight).
        void SetDefaultFamily(WideStringView name) { m_defaultFontFamily = WideString(name); }

        // --- IFontService --------------------------------------------------
        [[nodiscard]] WideStringView DefaultFontFamily() const override { return m_defaultFontFamily; }

        [[nodiscard]] CachedFont* GetFont(f32 pixelHeight) override
        {
            return GetFont(m_defaultFontFamily, pixelHeight);
        }

        [[nodiscard]] CachedFont* GetFont(WideStringView familyName, f32 pixelHeight) override
        {
            if (const FontEntry* exact = FindExact(familyName, pixelHeight))
                return exact->cachedFont;
            if (const FontEntry* closest = FindClosest(familyName, pixelHeight))
                return closest->cachedFont;
            return m_defaultFont;
        }

        [[nodiscard]] raptor::image::ImageData* GetAtlasTexture(CachedFont* font) override
        {
            for (FontEntry* entry : m_fonts)
                if (entry->cachedFont == font)
                    return entry->texture;
            return nullptr;
        }

        [[nodiscard]] raptor::image::ImageData* GetAtlasTexture(WideStringView familyName, f32 pixelHeight) override
        {
            if (const FontEntry* exact = FindExact(familyName, pixelHeight))
                return exact->texture;
            if (const FontEntry* closest = FindClosest(familyName, pixelHeight))
                return closest->texture;
            for (FontEntry* entry : m_fonts)
                if (entry->cachedFont == m_defaultFont)
                    return entry->texture;
            return nullptr;
        }

        // Fonts are owned by the service — releasing is a no-op.
        void ReleaseFont(CachedFont*) override {}

    private:
        struct FontEntry
        {
            WideString family;
            f32 pixelHeight = 0;
            CachedFont* cachedFont = nullptr;                 // owns font/atlas/shaper
            raptor::image::OwnedImageData* texture = nullptr; // owned
        };

        static void DeleteEntry(FontEntry* entry)
        {
            if (entry == nullptr) return;
            DefaultAllocator().Delete(entry->cachedFont); // frees font/atlas/shaper
            DefaultAllocator().Delete(entry->texture);
            DefaultAllocator().Delete(entry);
        }

        // ASCII case-insensitive family compare (matches Sedulous's ignore-case).
        static bool FamilyEquals(WideStringView a, WideStringView b)
        {
            if (a.Size() != b.Size())
                return false;
            for (usize i = 0; i < a.Size(); ++i)
            {
                widechar ca = a[i], cb = b[i];
                if (ca >= u'A' && ca <= u'Z') ca = static_cast<widechar>(ca - u'A' + u'a');
                if (cb >= u'A' && cb <= u'Z') cb = static_cast<widechar>(cb - u'A' + u'a');
                if (ca != cb)
                    return false;
            }
            return true;
        }

        // Bake the atlas, expand to RGBA8, wrap in a CachedFont, and record the
        // entry. Takes ownership of `font`; deletes it on any failure.
        FontLoadResult CacheFont(WideStringView familyName, IFont* font, FontLoadOptions options)
        {
            Result<IFontAtlas*, FontLoadResult> baked = FontAtlasBakerFactory::Bake(*font, options);
            if (!baked.HasValue())
            {
                DefaultAllocator().Delete(font);
                return baked.Error();
            }
            IFontAtlas* atlas = baked.Value();

            raptor::image::OwnedImageData* texture = FontAtlasTexture::ExpandR8ToRGBA8(atlas);
            if (texture == nullptr)
            {
                DefaultAllocator().Delete(atlas);
                DefaultAllocator().Delete(font);
                return FontLoadResult::OutOfMemory;
            }

            ITextShaper* shaper = DefaultAllocator().New<TrueTypeTextShaper>();
            CachedFont* cachedFont = DefaultAllocator().New<CachedFont>(font, atlas, shaper);

            FontEntry* entry = DefaultAllocator().New<FontEntry>();
            entry->family = WideString(familyName);
            entry->pixelHeight = options.pixelHeight;
            entry->cachedFont = cachedFont;
            entry->texture = texture;
            m_fonts.PushBack(entry);

            if (m_defaultFont == nullptr)
            {
                m_defaultFont = cachedFont;
                m_defaultFontFamily = WideString(familyName);
                m_defaultFontSize = options.pixelHeight;
            }
            return FontLoadResult::Success;
        }

        [[nodiscard]] const FontEntry* FindExact(WideStringView family, f32 pixelHeight) const
        {
            for (const FontEntry* entry : m_fonts)
                if (FamilyEquals(entry->family, family) && Abs(entry->pixelHeight - pixelHeight) < 0.001f)
                    return entry;
            return nullptr;
        }

        [[nodiscard]] const FontEntry* FindClosest(WideStringView family, f32 pixelHeight) const
        {
            const FontEntry* best = nullptr;
            f32 bestDiff = 3.4e38f;
            for (const FontEntry* entry : m_fonts)
                if (FamilyEquals(entry->family, family))
                {
                    const f32 diff = Abs(entry->pixelHeight - pixelHeight);
                    if (diff < bestDiff)
                    {
                        bestDiff = diff;
                        best = entry;
                    }
                }
            return best;
        }

        raptor::vfs::IFileSystem* m_fileSystem = nullptr; // non-owning
        Array<FontEntry*> m_fonts;
        WideString m_defaultFontFamily = WideString(u"Default");
        CachedFont* m_defaultFont = nullptr;
        f32 m_defaultFontSize = 16;
    };
}
