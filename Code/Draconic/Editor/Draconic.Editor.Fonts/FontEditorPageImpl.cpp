// Draconic::EditorFonts - the `draconic.editor.fonts` module (implementation).

module;
#include "Draconic.Core/Prelude.h"
#include "Draconic.Core/Log/Log.h"

module draconic.editor.fonts;

import draconic.core;
import draconic.content;
import draconic.vfs;
import draconic.image;
import draconic.fonts;
import draconic.fonts.ttf;
import draconic.fonts.io;
import draconic.fonts.importer;
import draconic.fonts.distancefield.baker;
import draconic.fonts.editor;
import draconic.ui;
import draconic.ui.toolkit;
import draconic.editor.core;
import draconic.editor.app;

using namespace draconic::core;

namespace draconic::editor
{
    namespace
    {
        constexpr StringView kModeItems[] = {u8"Raster Ramp", u8"Distance Field (MSDF)"};

        // "10, 12.5 24" -> floats; returns false (and leaves `out` untouched) on any junk.
        [[nodiscard]] bool ParseSizes(StringView text, Array<f32>& out)
        {
            Array<f32> parsed;
            f64 value = 0.0;
            f64 fraction = 0.0;
            bool inNumber = false;
            bool inFraction = false;
            for (usize i = 0; i <= text.Size(); ++i)
            {
                const utf8char c = i < text.Size() ? text[i] : utf8char(',');
                if (c >= u8'0' && c <= u8'9')
                {
                    if (inFraction)
                    {
                        fraction *= 0.1;
                        value += (c - u8'0') * fraction;
                    }
                    else
                    {
                        value = value * 10.0 + (c - u8'0');
                    }
                    inNumber = true;
                }
                else if (c == u8'.' && inNumber && !inFraction)
                {
                    inFraction = true;
                    fraction = 1.0;
                }
                else if (c == u8',' || c == u8' ' || c == u8'\t' || c == u8';')
                {
                    if (inNumber)
                    {
                        if (value <= 0.0 || value > 512.0)
                        {
                            return false;
                        }
                        parsed.PushBack(static_cast<f32>(value));
                        value = 0.0;
                        inNumber = false;
                        inFraction = false;
                    }
                }
                else
                {
                    return false; // junk character
                }
            }
            if (parsed.IsEmpty())
            {
                return false;
            }
            out = Move(parsed);
            return true;
        }

        [[nodiscard]] String FormatSizes(const Array<f32>& sizes)
        {
            String text;
            for (usize i = 0; i < sizes.Size(); ++i)
            {
                if (i != 0)
                {
                    text += u8", ";
                }
                const f32 s = sizes[i];
                const i32 whole = static_cast<i32>(s);
                const i32 tenth = static_cast<i32>((s - static_cast<f32>(whole)) * 10.0f + 0.5f);
                text += (tenth != 0) ? Format(u8"{}.{}", whole, tenth) : Format(u8"{}", whole);
            }
            return text;
        }
    } // namespace

    FontEditorPage::FontEditorPage(EditorContext& context, draconic::content::Instance& instance)
        : m_context(&context), m_title(instance.Name())
    {
        SetInstanceId(instance.Id());

        RefPtr<ISerializable> object = instance.ReadObject();
        m_asset = RefPtr<fonts::FontAsset>(Cast<fonts::FontAsset>(object.Get()));
        if (m_asset.Get() == nullptr)
        {
            DRACONIC_LOG_ERROR(u8"Editor", u8"font '{}' failed to read - page opens empty",
                               m_title);
        }
        m_undoBaseline = Snapshot();

        m_image = MakeRef<ui::ImageView>(DefaultAllocator());
        m_image->ScaleType.SetValue(ui::ScaleType::FitCenter);

        m_info = MakeRef<ui::Label>(DefaultAllocator(), StringView(u8""));
        m_info->FontSize.SetValue(12.0f);

        auto previewColumn = MakeRef<ui::FlexLayout>(DefaultAllocator());
        previewColumn->Direction = ui::Orientation::Vertical;
        previewColumn->Spacing = 6.0f;
        previewColumn->Padding = ui::Thickness{8, 6};
        {
            auto lp = MakeRef<ui::FlexLayoutParams>(DefaultAllocator());
            lp->Width = ui::SizeSpec::Match();
            previewColumn->AddView(m_info.Get(), lp);
        }
        {
            auto lp = MakeRef<ui::FlexLayoutParams>(DefaultAllocator());
            lp->Width = ui::SizeSpec::Match();
            lp->Grow = 1.0f;
            previewColumn->AddView(m_image.Get(), lp);
        }

        m_grid = MakeRef<ui::toolkit::PropertyGrid>(DefaultAllocator());
        BuildGrid();

        auto gridColumn = MakeRef<ui::FlexLayout>(DefaultAllocator());
        gridColumn->Direction = ui::Orientation::Vertical;
        gridColumn->Padding = ui::Thickness{8, 6};
        {
            auto lp = MakeRef<ui::FlexLayoutParams>(DefaultAllocator());
            lp->Grow = 1.0f;
            gridColumn->AddView(m_grid.Get(), lp);
        }

        auto split = MakeRef<ui::toolkit::SplitView>(DefaultAllocator());
        split->SetSplitRatio(0.6f);
        split->SetPanes(previewColumn.Get(), gridColumn.Get());
        m_content = split;

        RebakePreview();
    }

    void FontEditorPage::RebakePreview()
    {
        m_preview = nullptr;
        m_previewGlyphs = 0;
        m_previewSize = 0.0f;
        if (m_asset.Get() != nullptr && !m_asset->fileName.IsEmpty() &&
            m_context->Project() != nullptr)
        {
            const String path =
                PathJoin(m_context->Project()->SourcesRoot().AsView(), m_asset->fileName.AsView());
            Result<Array<byte>> bytes = ReadFile(path.AsView());
            if (bytes.HasValue())
            {
                const Span<const u8> fontBytes(
                    reinterpret_cast<const u8*>(bytes.Value().Data()), bytes.Value().Size());
                const bool distanceField = m_asset->mode == fonts::FontBakeMode::DistanceField;
                // Coverage previews at the ramp's LARGEST size (the most informative atlas).
                f32 size = m_asset->dfSize;
                if (!distanceField)
                {
                    size = 14.0f;
                    for (f32 s : m_asset->sizes)
                    {
                        size = Max(size, s);
                    }
                }
                m_previewSize = size;

                fonts::FontLoadOptions options = distanceField
                                                     ? fonts::FontLoadOptions::DistanceField()
                                                     : fonts::FontLoadOptions::Default();
                options.pixelHeight = size;
                options.firstCodepoint = m_asset->firstCodepoint;
                options.lastCodepoint = m_asset->lastCodepoint;
                options.atlasWidth = m_asset->atlasWidth;
                options.atlasHeight = m_asset->atlasHeight;

                if (distanceField)
                {
                    fonts::DFFonts::Initialize();
                    Array<u8> copy;
                    copy.Resize(fontBytes.Size());
                    if (fontBytes.Size() != 0)
                    {
                        MemCopy(copy.Data(), fontBytes.Data(), fontBytes.Size());
                    }
                    fonts::TrueTypeFont font;
                    if (font.Initialize(Move(copy), options.pixelHeight) ==
                        fonts::FontLoadResult::Success)
                    {
                        Result<fonts::IFontAtlas*, fonts::FontLoadResult> baked =
                            fonts::FontAtlasBakerFactory::Bake(font, options);
                        if (baked.HasValue())
                        {
                            UniquePtr<fonts::IFontAtlas> atlas(baked.Value(), DefaultAllocator());
                            for (i32 cp = options.firstCodepoint; cp <= options.lastCodepoint;
                                 ++cp)
                            {
                                if (atlas->Contains(cp))
                                {
                                    ++m_previewGlyphs;
                                }
                            }
                            m_preview = MakeUnique<image::OwnedImageData>(
                                DefaultAllocator(), atlas->Width(), atlas->Height(),
                                image::PixelFormat::RGBA8, atlas->PixelData(),
                                image::ImageColorSpace::Linear);
                        }
                    }
                }
                else
                {
                    Result<fonts::BakedFontData*, fonts::FontLoadResult> baked =
                        fonts::FontImporter::Bake(fontBytes, options);
                    if (baked.HasValue())
                    {
                        UniquePtr<fonts::BakedFontData> data(baked.Value(), DefaultAllocator());
                        m_previewGlyphs = data->atlas->Regions().Size();
                        m_preview = UniquePtr<image::OwnedImageData>(
                            fonts::FontAtlasTexture::ExpandR8ToRGBA8(data->atlas),
                            DefaultAllocator());
                    }
                }
            }
        }

        m_image->SetImage(m_preview.Get());
        if (m_asset.Get() == nullptr)
        {
            m_info->SetText(u8"Font failed to load.");
        }
        else if (m_preview.Get() == nullptr)
        {
            m_info->SetText(u8"No preview (source file missing, unparseable, or the atlas "
                            u8"is too small for the range).");
        }
        else
        {
            const i32 sizeWhole = static_cast<i32>(m_previewSize);
            m_info->SetText(
                Format(u8"{}  |  {} glyphs @ {}px  |  atlas {} x {}", m_asset->fileName,
                       static_cast<u64>(m_previewGlyphs), sizeWhole, m_preview->Width(),
                       m_preview->Height())
                    .AsView());
        }
    }

    void FontEditorPage::CommitEdit(StringView mergeKey)
    {
        Array<byte> after = Snapshot();
        (void)Commands().Execute(UniquePtr<IEditorCommand>(
            DefaultAllocator().New<EditFontCommand>(*this, mergeKey, m_undoBaseline, after),
            DefaultAllocator()));
        m_undoBaseline = Move(after);
        MarkDirty();
        RebakePreview();
    }

    void FontEditorPage::BuildGrid()
    {
        m_grid->Clear();
        if (m_asset.Get() == nullptr)
        {
            return;
        }
        FontEditorPage* self = this;

        auto family = MakeRef<ui::toolkit::StringEditor>(
            DefaultAllocator(), u8"Family", m_asset->family.AsView(),
            Function<void(StringView)>{[self](StringView v)
                                       {
                                           self->m_asset->family = String(v);
                                           self->CommitEdit(u8"family");
                                       }},
            u8"Font");
        m_familyRow = family.Get();
        m_grid->AddProperty(RefPtr<ui::toolkit::PropertyEditor>(family.Get()));

        auto mode = MakeRef<ui::toolkit::EnumEditor>(
            DefaultAllocator(), u8"Bake Mode", static_cast<i32>(m_asset->mode),
            Span<const StringView>{kModeItems, 2},
            Function<void(i32)>{[self](i32 v)
                                {
                                    self->m_asset->mode = static_cast<fonts::FontBakeMode>(v);
                                    self->CommitEdit(u8"mode");
                                }},
            u8"Font");
        m_modeRow = mode.Get();
        m_grid->AddProperty(RefPtr<ui::toolkit::PropertyEditor>(mode.Get()));

        auto sizes = MakeRef<ui::toolkit::StringEditor>(
            DefaultAllocator(), u8"Sizes (px)", FormatSizes(m_asset->sizes).AsView(),
            Function<void(StringView)>{[self](StringView v)
                                       {
                                           Array<f32> parsed;
                                           if (!ParseSizes(v, parsed))
                                           {
                                               // junk input: re-pull the last good value
                                               self->RefreshRows();
                                               return;
                                           }
                                           self->m_asset->sizes = Move(parsed);
                                           self->CommitEdit(u8"sizes");
                                       }},
            u8"Raster Ramp");
        m_sizesRow = sizes.Get();
        m_grid->AddProperty(RefPtr<ui::toolkit::PropertyEditor>(sizes.Get()));

        auto dfSize = MakeRef<ui::toolkit::FloatEditor>(
            DefaultAllocator(), u8"MSDF Size (px)", static_cast<f64>(m_asset->dfSize), 8.0, 128.0,
            1.0, 0,
            Function<void(f64)>{[self](f64 v)
                                {
                                    self->m_asset->dfSize = static_cast<f32>(v);
                                    self->CommitEdit(u8"df-size");
                                }},
            u8"Distance Field");
        m_dfSizeRow = dfSize.Get();
        m_grid->AddProperty(RefPtr<ui::toolkit::PropertyEditor>(dfSize.Get()));

        auto first = MakeRef<ui::toolkit::IntEditor>(
            DefaultAllocator(), u8"First Codepoint", static_cast<i64>(m_asset->firstCodepoint), 0,
            0x10FFFF,
            Function<void(i64)>{[self](i64 v)
                                {
                                    self->m_asset->firstCodepoint = static_cast<i32>(v);
                                    self->CommitEdit(u8"first-cp");
                                }},
            u8"Glyph Range");
        m_firstRow = first.Get();
        m_grid->AddProperty(RefPtr<ui::toolkit::PropertyEditor>(first.Get()));

        auto last = MakeRef<ui::toolkit::IntEditor>(
            DefaultAllocator(), u8"Last Codepoint", static_cast<i64>(m_asset->lastCodepoint), 0,
            0x10FFFF,
            Function<void(i64)>{[self](i64 v)
                                {
                                    self->m_asset->lastCodepoint = static_cast<i32>(v);
                                    self->CommitEdit(u8"last-cp");
                                }},
            u8"Glyph Range");
        m_lastRow = last.Get();
        m_grid->AddProperty(RefPtr<ui::toolkit::PropertyEditor>(last.Get()));

        auto atlasWidth = MakeRef<ui::toolkit::IntEditor>(
            DefaultAllocator(), u8"Atlas Width", static_cast<i64>(m_asset->atlasWidth), 64, 8192,
            Function<void(i64)>{[self](i64 v)
                                {
                                    self->m_asset->atlasWidth = static_cast<u32>(v);
                                    self->CommitEdit(u8"atlas-w");
                                }},
            u8"Atlas");
        m_atlasWidthRow = atlasWidth.Get();
        m_grid->AddProperty(RefPtr<ui::toolkit::PropertyEditor>(atlasWidth.Get()));

        auto atlasHeight = MakeRef<ui::toolkit::IntEditor>(
            DefaultAllocator(), u8"Atlas Height", static_cast<i64>(m_asset->atlasHeight), 64, 8192,
            Function<void(i64)>{[self](i64 v)
                                {
                                    self->m_asset->atlasHeight = static_cast<u32>(v);
                                    self->CommitEdit(u8"atlas-h");
                                }},
            u8"Atlas");
        m_atlasHeightRow = atlasHeight.Get();
        m_grid->AddProperty(RefPtr<ui::toolkit::PropertyEditor>(atlasHeight.Get()));

        // Read-only source fact.
        m_grid->AddProperty(RefPtr<ui::toolkit::PropertyEditor>(
            MakeRef<ui::toolkit::StringEditor>(DefaultAllocator(), u8"File",
                                               m_asset->fileName.AsView(),
                                               Function<void(StringView)>{}, u8"Source")
                .Get()));
    }

    Array<byte> FontEditorPage::Snapshot() const
    {
        Array<byte> blob;
        if (m_asset.Get() == nullptr)
        {
            return blob;
        }
        MemoryStream stream;
        BinarySerializer ar(stream, SerializeMode::Write);
        m_asset->Serialize(ar);
        const Span<const byte> bytes = stream.Bytes();
        blob.Reserve(bytes.Size());
        for (byte b : bytes)
        {
            blob.PushBack(b);
        }
        return blob;
    }

    void FontEditorPage::ApplyBlob(const Array<byte>& blob)
    {
        if (m_asset.Get() == nullptr)
        {
            return;
        }
        Array<byte> current = Snapshot();
        if (current.Size() == blob.Size())
        {
            bool same = true;
            for (usize i = 0; i < blob.Size(); ++i)
            {
                if (current[i] != blob[i])
                {
                    same = false;
                    break;
                }
            }
            if (same)
            {
                return;
            }
        }
        MemoryStream stream;
        (void)stream.Write(blob.Data(), blob.Size());
        (void)stream.Seek(0, SeekOrigin::Begin);
        BinarySerializer ar(stream, SerializeMode::Read);
        m_asset->sizes.Clear(); // ISerializable array: restore clears in place
        m_asset->Serialize(ar);
        m_undoBaseline = blob;
        RefreshRows();
        MarkDirty();
        RebakePreview();
    }

    void FontEditorPage::RefreshRows()
    {
        if (m_asset.Get() == nullptr)
        {
            return;
        }
        if (m_familyRow != nullptr)
        {
            m_familyRow->SetValue(m_asset->family.AsView());
        }
        if (m_modeRow != nullptr)
        {
            m_modeRow->SetValue(static_cast<i32>(m_asset->mode));
        }
        if (m_sizesRow != nullptr)
        {
            m_sizesRow->SetValue(FormatSizes(m_asset->sizes).AsView());
        }
        if (m_dfSizeRow != nullptr)
        {
            m_dfSizeRow->SetValue(static_cast<f64>(m_asset->dfSize));
        }
        if (m_firstRow != nullptr)
        {
            m_firstRow->SetValue(static_cast<i64>(m_asset->firstCodepoint));
        }
        if (m_lastRow != nullptr)
        {
            m_lastRow->SetValue(static_cast<i64>(m_asset->lastCodepoint));
        }
        if (m_atlasWidthRow != nullptr)
        {
            m_atlasWidthRow->SetValue(static_cast<i64>(m_asset->atlasWidth));
        }
        if (m_atlasHeightRow != nullptr)
        {
            m_atlasHeightRow->SetValue(static_cast<i64>(m_asset->atlasHeight));
        }
    }

    Status FontEditorPage::Save()
    {
        if (m_asset.Get() == nullptr || m_context->Project() == nullptr)
        {
            return Status{ErrorCode::NotFound};
        }
        draconic::content::Instance* instance =
            m_context->Project()->SourceDb().GetInstance(InstanceId());
        if (instance == nullptr)
        {
            return Status{ErrorCode::NotFound};
        }
        const Status saved = instance->WriteObject(*m_asset);
        if (saved.IsOk())
        {
            ClearDirty();
            m_context->RequestCook(false);
            DRACONIC_LOG_INFO(u8"Editor", u8"saved font '{}'", m_title);
        }
        return saved;
    }

    const TypeInfo* FontEditorPageFactory::PrimaryType() const
    {
        return &fonts::FontAsset::StaticType();
    }

    UniquePtr<EditorPage> FontEditorPageFactory::CreatePage(EditorContext& context,
                                                            draconic::content::Instance& instance)
    {
        auto* page = DefaultAllocator().New<FontEditorPage>(context, instance);
        return UniquePtr<EditorPage>(page, DefaultAllocator());
    }
}
