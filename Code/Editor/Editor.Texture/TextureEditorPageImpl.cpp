// SPDX-License-Identifier: MIT
// Copyright (c) 2026-Present Robert Campbell

// Editor::Texture - the `editor.texture` module (implementation).

module;
#include "Core/Prelude.h"
#include "Core/Log/Log.h"

module editor.texture;

import foundation.core;
import foundation.content;
import foundation.image;
import foundation.image.io;
import foundation.texture;
import texture.pipeline;
import texture.compression;
import foundation.ui;
import foundation.ui.toolkit;
import editor.core;
import foundation.rhi;
import editor.app;

using namespace foundation::core;
namespace image = foundation::image;
namespace texture = foundation::texture;
namespace ui = foundation::ui;

namespace editor
{
    namespace
    {
        // Sampler/shape option labels (index == the enum's underlying value, both 0-based).
        constexpr StringView kColorSpaceItems[] = {u8"sRGB", u8"Linear"};
        constexpr StringView kShapeItems[] = {u8"2D", u8"2D Array", u8"3D", u8"Cubemap",
                                              u8"Cubemap Array"};
        constexpr StringView kFilterItems[] = {u8"Nearest", u8"Linear", u8"Mipmap Nearest",
                                               u8"Mipmap Linear"};
        constexpr StringView kWrapItems[] = {u8"Repeat", u8"Clamp To Edge", u8"Clamp To Border",
                                             u8"Mirrored Repeat"};
        // Block-compression authoring (asset-variants). Index == the texcomp enum's value;
        // labels speak the author's language, not the enum's.
        constexpr StringView kUsageItems[] = {u8"Color", u8"Normal map",
                                              u8"Data mask (rough/AO/height/coverage)", u8"HDR"};
        constexpr StringView kCompressionItems[] = {u8"Default", u8"None", u8"Quality"};

        // The display name of a policy-resolved cooked format (the "Cooks to:" row).
        [[nodiscard]] StringView CookedFormatName(foundation::rhi::TextureFormat format)
        {
            switch (format)
            {
            case foundation::rhi::TextureFormat::BC1RGBAUnorm: return u8"BC1 (opaque)";
            case foundation::rhi::TextureFormat::BC1RGBAUnormSrgb: return u8"BC1 sRGB (opaque)";
            case foundation::rhi::TextureFormat::BC4RUnorm: return u8"BC4 (single channel)";
            case foundation::rhi::TextureFormat::BC5RGUnorm: return u8"BC5 (normal RG)";
            case foundation::rhi::TextureFormat::BC7RGBAUnorm: return u8"BC7";
            case foundation::rhi::TextureFormat::BC7RGBAUnormSrgb: return u8"BC7 sRGB";
            case foundation::rhi::TextureFormat::ASTC4x4Unorm: return u8"ASTC 4x4";
            case foundation::rhi::TextureFormat::ASTC4x4UnormSrgb: return u8"ASTC 4x4 sRGB";
            case foundation::rhi::TextureFormat::RGBA32Float: return u8"RGBA32F (uncompressed)";
            default: return u8"uncompressed";
            }
        }

        // Convert a decoded source image to an RGBA8 CPU buffer for the ImageView (the VG image
        // path is 8-bit; HDR sources are tonemapped by a plain clamp - a faithful preview needs
        // no exposure control here).
        UniquePtr<image::OwnedImageData> ToPreview(const image::Image& src)
        {
            const u32 w = src.Width();
            const u32 h = src.Height();
            if (w == 0 || h == 0)
            {
                return {};
            }
            if (src.Format() == image::PixelFormat::RGBA8)
            {
                return MakeUnique<image::OwnedImageData>(editor::EditorRootAllocator(), w, h,
                                                         image::PixelFormat::RGBA8, src.PixelData(),
                                                         src.ColorSpace());
            }
            if (src.Format() == image::PixelFormat::RGBA32F)
            {
                const Span<const u8> raw = src.PixelData();
                const usize texels = static_cast<usize>(w) * h * 4;
                if (raw.Size() < texels * sizeof(f32))
                {
                    return {};
                }
                const f32* in = reinterpret_cast<const f32*>(raw.Data());
                Array<u8> out;
                out.Resize(texels);
                for (usize i = 0; i < texels; ++i)
                {
                    const f32 c = Clamp(in[i], 0.0f, 1.0f);
                    out[i] = static_cast<u8>(c * 255.0f + 0.5f);
                }
                return MakeUnique<image::OwnedImageData>(editor::EditorRootAllocator(), w, h,
                                                         image::PixelFormat::RGBA8, Move(out),
                                                         image::ImageColorSpace::Srgb);
            }
            return {};
        }
    } // namespace

    TextureEditorPage::TextureEditorPage(EditorContext& context,
                                         foundation::content::Instance& instance)
        : app::UIEditorPage(context.Allocator()),
          m_context(&context), m_title(instance.Name())
    {
        SetInstanceId(instance.Id());

        RefPtr<ISerializable> object = instance.ReadObject();
        m_asset = RefPtr<pipeline::TextureAsset>(Cast<pipeline::TextureAsset>(object.Get()));
        if (m_asset.Get() == nullptr)
        {
            LOG_ERROR(u8"Editor", u8"texture '{}' failed to read - page opens empty",
                               m_title);
        }
        else
        {
            LoadPreview(instance);
        }

        // Left: source facts label + the preview image (fit-centered on a dark panel).
        m_image = MakeRef<ui::ImageView>(Allocator());
        m_image->ScaleType.SetValue(ui::ScaleType::FitCenter);
        m_image->SetImage(m_preview.Get());

        m_info = MakeRef<ui::Label>(Allocator(), StringView(u8""));
        m_info->FontSize.SetValue(12.0f);

        auto previewColumn = MakeRef<ui::FlexLayout>(Allocator());
        previewColumn->Direction = ui::Orientation::Vertical;
        previewColumn->Spacing = 6.0f;
        previewColumn->Padding = ui::Thickness{8, 6};
        {
            auto lp = MakeRef<ui::FlexLayoutParams>(Allocator());
            lp->Width = ui::SizeSpec::Match();
            previewColumn->AddView(m_info.Get(), lp);
        }
        {
            auto lp = MakeRef<ui::FlexLayoutParams>(Allocator());
            lp->Width = ui::SizeSpec::Match();
            lp->Grow = 1.0f;
            previewColumn->AddView(m_image.Get(), lp);
        }

        // Right: the property grid, inset off the pane edge like the material/scene inspectors.
        m_grid = MakeRef<ui::toolkit::PropertyGrid>(Allocator());
        BuildGrid();

        auto gridColumn = MakeRef<ui::FlexLayout>(Allocator());
        gridColumn->Direction = ui::Orientation::Vertical;
        gridColumn->Padding = ui::Thickness{8, 6};
        gridColumn->Spacing = 6.0f;
        {
            auto profileRow = MakeRef<ui::FlexLayout>(Allocator());
            profileRow->Direction = ui::Orientation::Horizontal;
            profileRow->Spacing = 4.0f;
            BuildProfileRow(*profileRow);
            auto lp = MakeRef<ui::FlexLayoutParams>(Allocator());
            lp->Width = ui::SizeSpec::Match();
            gridColumn->AddView(profileRow.Get(), lp);
        }
        {
            auto lp = MakeRef<ui::FlexLayoutParams>(Allocator());
            lp->Grow = 1.0f;
            gridColumn->AddView(m_grid.Get(), lp);
        }

        auto split = MakeRef<ui::toolkit::SplitView>(Allocator());
        split->SetSplitRatio(0.55f);
        split->SetPanes(previewColumn.Get(), gridColumn.Get());

        // The page action bar (Save / Undo / Redo / Discard) above the split.
        m_toolbar = MakeRef<app::PageToolbar>(Allocator(), *this);
        auto pageColumn = MakeRef<ui::FlexLayout>(Allocator());
        pageColumn->Direction = ui::Orientation::Vertical;
        {
            auto lp = MakeRef<ui::FlexLayoutParams>(Allocator());
            lp->Width = ui::SizeSpec::Match();
            pageColumn->AddView(m_toolbar.Get(), lp);
            auto grow = MakeRef<ui::FlexLayoutParams>(Allocator());
            grow->Grow = 1.0f;
            grow->Width = ui::SizeSpec::Match();
            pageColumn->AddView(split.Get(), grow);
        }
        m_content = pageColumn;

        RefreshInfo();
    }

    void TextureEditorPage::LoadPreview(foundation::content::Instance& instance)
    {
        if (m_asset.Get() == nullptr)
        {
            return;
        }

        // Embedded mode (model imports): the "pixels" stream is raw RGBA8 at embeddedWidth/Height.
        if (m_asset->fileName.IsEmpty() && m_asset->embeddedWidth > 0 &&
            m_asset->embeddedHeight > 0)
        {
            UniquePtr<IStream> stream = instance.ReadData(u8"pixels");
            if (!stream)
            {
                return;
            }
            const i64 size = stream->Size();
            const usize expected =
                static_cast<usize>(m_asset->embeddedWidth) * m_asset->embeddedHeight * 4;
            if (size <= 0 || static_cast<usize>(size) < expected)
            {
                return;
            }
            Array<u8> pixels;
            pixels.Resize(expected);
            if (stream->Read(pixels.Data(), expected) != expected)
            {
                return;
            }
            m_sourceFormat = image::PixelFormat::RGBA8;
            m_preview = MakeUnique<image::OwnedImageData>(
                Allocator(), m_asset->embeddedWidth, m_asset->embeddedHeight,
                image::PixelFormat::RGBA8, Move(pixels), m_asset->colorSpace);
            return;
        }

        // External file: decode the source (the +X face stands in for a cubemap preview).
        if (m_asset->fileName.IsEmpty() || m_context->Project() == nullptr)
        {
            return;
        }
        const String path =
            PathJoin(m_context->Project()->SourcesRoot().AsView(), m_asset->fileName.View());
        image::Image image;
        const Status loaded = image::io::LoadImage(path.AsView(), image);
        if (!loaded.IsOk())
        {
            LOG_WARNING(u8"Editor", u8"texture source missing or undecodable: {}", path);
            return;
        }
        m_sourceFormat = image.Format();
        m_preview = ToPreview(image);
    }

    void TextureEditorPage::RefreshInfo()
    {
        if (m_asset.Get() == nullptr)
        {
            m_info->SetText(u8"Texture failed to load.");
            return;
        }
        if (m_preview.Get() == nullptr)
        {
            m_info->SetText(u8"No preview (source file missing, embedded, or undecodable).");
            return;
        }
        // The GPU format the cook resolves from the source format + the chosen color space.
        const bool srgb = m_asset->colorSpace == image::ImageColorSpace::Srgb;
        StringView derived;
        if (m_sourceFormat == image::PixelFormat::RGBA32F)
        {
            derived = u8"RGBA32F";
        }
        else
        {
            derived = srgb ? StringView(u8"RGBA8 sRGB") : StringView(u8"RGBA8 Linear");
        }
        // Mip chain the cook will produce (full chain to 1x1 when enabled - 2026-08-12).
        u32 mipLevels = 1;
        if (m_asset->generateMipmaps)
        {
            u32 w = m_preview->Width();
            u32 h = m_preview->Height();
            while (w > 1 || h > 1)
            {
                w = w > 1 ? w / 2 : 1;
                h = h > 1 ? h / 2 : 1;
                ++mipLevels;
            }
        }
        String text = Format(u8"{} x {}  |  source {}  |  cooks to {}  |  {} mip level(s)",
                             m_preview->Width(), m_preview->Height(),
                             m_sourceFormat == image::PixelFormat::RGBA32F ? StringView(u8"HDR")
                                                                           : StringView(u8"LDR"),
                             derived, static_cast<u64>(mipLevels));
        m_info->SetText(text.AsView());
    }

    void TextureEditorPage::BuildGrid()
    {
        m_grid->Clear();
        m_refreshers.Clear();
        if (m_asset.Get() == nullptr)
        {
            return;
        }
        TextureEditorPage* self = this;

        // (Profiles are BUTTONS above the grid - an action, not a property; see BuildProfileRow.)


        // --- Content: WHAT the texture is. Usage is the primary question; Color Space
        // derives from it (Normal/Mask/HDR are always Linear; Color implies sRGB) in the
        // SAME undo entry, with the demoted advanced row below for the rare override. ---
        {
            auto editor = MakeRef<ui::toolkit::EnumEditor>(
                Allocator(), StringView(u8"Usage"), static_cast<i32>(m_asset->usage),
                Span<const StringView>{kUsageItems, 4},
                Function<void(i32)>{
                    [self](i32 v)
                    {
                        self->ApplyEdit(u8"usage",
                                        Function<void(pipeline::TextureAsset&)>{
                                            [v](pipeline::TextureAsset& a)
                                            {
                                                a.usage = static_cast<texcomp::TextureUsage>(v);
                                                a.colorSpace =
                                                    a.usage == texcomp::TextureUsage::Color
                                                        ? image::ImageColorSpace::Srgb
                                                        : image::ImageColorSpace::Linear;
                                            }});
                    }},
                StringView(u8"Content"));
            AddEditor(editor.Get(), [self, raw = editor.Get()]()
                      { raw->SetValue(static_cast<i32>(self->m_asset->usage)); });
        }
        {
            // The one legitimate override (linear color data, e.g. a LUT authored as an image).
            auto editor = MakeRef<ui::toolkit::EnumEditor>(
                Allocator(), StringView(u8"Color Space (advanced)"),
                static_cast<i32>(m_asset->colorSpace), Span<const StringView>{kColorSpaceItems, 2},
                Function<void(i32)>{
                    [self](i32 v)
                    {
                        self->ApplyEdit(
                            u8"colorSpace",
                            Function<void(pipeline::TextureAsset&)>{
                                [v](pipeline::TextureAsset& a)
                                { a.colorSpace = static_cast<image::ImageColorSpace>(v); }});
                    }},
                StringView(u8"Content"));
            AddEditor(editor.Get(), [self, raw = editor.Get()]()
                      { raw->SetValue(static_cast<i32>(self->m_asset->colorSpace)); });
        }
        {
            // The mismatch lint: presentation only - it fires on assets that already carry a
            // semantically wrong combination, which is the point.
            auto lint = MakeRef<ui::toolkit::StringEditor>(
                Allocator(), StringView(u8"Warning"), StringView(u8""),
                Function<void(StringView)>{}, StringView(u8"Content"));
            ui::toolkit::StringEditor* lintRaw = lint.Get();
            AddEditor(lint.Get(),
                      [self, lintRaw]()
                      {
                          const bool dataAsSrgb =
                              self->m_asset->usage != texcomp::TextureUsage::Color &&
                              self->m_asset->colorSpace == image::ImageColorSpace::Srgb;
                          const bool colorAsLinear =
                              self->m_asset->usage == texcomp::TextureUsage::Color &&
                              self->m_asset->colorSpace == image::ImageColorSpace::Linear;
                          lintRaw->SetRowVisible(dataAsSrgb || colorAsLinear);
                          if (dataAsSrgb)
                          {
                              lintRaw->SetValue(
                                  u8"Normal/Mask/HDR maps are data - sRGB will warp the values. "
                                  u8"Set Linear.");
                          }
                          else if (colorAsLinear)
                          {
                              lintRaw->SetValue(
                                  u8"Color as Linear is unusual (fine for LUT-style data).");
                          }
                      });
            lintRaw->SetRowVisible(false);
        }
        {
            auto editor = MakeRef<ui::toolkit::EnumEditor>(
                Allocator(), StringView(u8"Compression"),
                static_cast<i32>(m_asset->compression), Span<const StringView>{kCompressionItems, 3},
                Function<void(i32)>{
                    [self](i32 v)
                    {
                        self->ApplyEdit(
                            u8"compression",
                            Function<void(pipeline::TextureAsset&)>{
                                [v](pipeline::TextureAsset& a)
                                { a.compression = static_cast<texcomp::CompressionChoice>(v); }});
                    }},
                StringView(u8"Content"));
            AddEditor(editor.Get(), [self, raw = editor.Get()]()
                      { raw->SetValue(static_cast<i32>(self->m_asset->compression)); });
        }
        if (!m_asset->sourceHint.IsEmpty())
        {
            // Provenance for model fan-out textures: the asset NAME prefers the file stem,
            // but a review rename or an authored-name fallback can diverge from the file -
            // this row always tells the truth about where the pixels came from.
            auto source = MakeRef<ui::toolkit::StringEditor>(
                Allocator(), StringView(u8"Source"), m_asset->sourceHint.AsView(),
                Function<void(StringView)>{}, StringView(u8"Content"));
            AddEditor(source.Get(), [self, raw = source.Get()]()
                      { raw->SetValue(self->m_asset->sourceHint.AsView()); });
        }
        {
            // "Default" stops being opaque: the policy evaluated for the desktop profile (and
            // the mobile one when it differs). Read-only; recomputed after every edit.
            auto cooks = MakeRef<ui::toolkit::StringEditor>(
                Allocator(), StringView(u8"Cooks to"), StringView(u8""),
                Function<void(StringView)>{}, StringView(u8"Content"));
            ui::toolkit::StringEditor* cooksRaw = cooks.Get();
            AddEditor(cooks.Get(),
                      [self, cooksRaw]() { cooksRaw->SetValue(self->ResolvedFormatText().AsView()); });
            cooksRaw->SetValue(ResolvedFormatText().AsView());
        }
        {
            auto editor = MakeRef<ui::toolkit::EnumEditor>(
                Allocator(), StringView(u8"Shape"), static_cast<i32>(m_asset->shape),
                Span<const StringView>{kShapeItems, 5},
                Function<void(i32)>{
                    [self](i32 v)
                    {
                        self->ApplyEdit(u8"shape",
                                        Function<void(pipeline::TextureAsset&)>{
                                            [v](pipeline::TextureAsset& a)
                                            { a.shape = static_cast<texture::TextureShape>(v); }});
                    }},
                StringView(u8"Sampling"));
            AddEditor(editor.Get(), [self, raw = editor.Get()]()
                      { raw->SetValue(static_cast<i32>(self->m_asset->shape)); });
        }

        // --- Sampling: filters, wraps, mipmaps, anisotropy ---
        auto addFilterRow = [self](StringView label, StringView key,
                                   texture::TextureFilter pipeline::TextureAsset::* field)
        {
            auto editor = MakeRef<ui::toolkit::EnumEditor>(
                self->Allocator(), label, static_cast<i32>(self->m_asset.Get()->*field),
                Span<const StringView>{kFilterItems, 4},
                Function<void(i32)>{
                    [self, field, key = String(key)](i32 v)
                    {
                        self->ApplyEdit(
                            key.AsView(),
                            Function<void(pipeline::TextureAsset&)>{
                                [field, v](pipeline::TextureAsset& a)
                                { a.*field = static_cast<texture::TextureFilter>(v); }});
                    }},
                StringView(u8"Sampling"));
            self->AddEditor(editor.Get(), [self, raw = editor.Get(), field]()
                            { raw->SetValue(static_cast<i32>(self->m_asset.Get()->*field)); });
        };
        addFilterRow(u8"Min Filter", u8"minFilter", &pipeline::TextureAsset::minFilter);
        addFilterRow(u8"Mag Filter", u8"magFilter", &pipeline::TextureAsset::magFilter);

        auto addWrapRow = [self](StringView label, StringView key,
                                 texture::TextureWrap pipeline::TextureAsset::* field)
        {
            auto editor = MakeRef<ui::toolkit::EnumEditor>(
                self->Allocator(), label, static_cast<i32>(self->m_asset.Get()->*field),
                Span<const StringView>{kWrapItems, 4},
                Function<void(i32)>{
                    [self, field, key = String(key)](i32 v)
                    {
                        self->ApplyEdit(key.AsView(),
                                        Function<void(pipeline::TextureAsset&)>{
                                            [field, v](pipeline::TextureAsset& a)
                                            { a.*field = static_cast<texture::TextureWrap>(v); }});
                    }},
                StringView(u8"Sampling"));
            self->AddEditor(editor.Get(), [self, raw = editor.Get(), field]()
                            { raw->SetValue(static_cast<i32>(self->m_asset.Get()->*field)); });
        };
        addWrapRow(u8"Wrap U", u8"wrapU", &pipeline::TextureAsset::wrapU);
        addWrapRow(u8"Wrap V", u8"wrapV", &pipeline::TextureAsset::wrapV);
        addWrapRow(u8"Wrap W", u8"wrapW", &pipeline::TextureAsset::wrapW);

        {
            auto editor = MakeRef<ui::toolkit::BoolEditor>(
                Allocator(), StringView(u8"Generate Mipmaps"), m_asset->generateMipmaps,
                Function<void(bool)>{[self](bool v)
                                     {
                                         self->ApplyEdit(u8"generateMipmaps",
                                                         Function<void(pipeline::TextureAsset&)>{
                                                             [v](pipeline::TextureAsset& a)
                                                             { a.generateMipmaps = v; }});
                                     }},
                StringView(u8"Sampling"));
            AddEditor(editor.Get(), [self, raw = editor.Get()]()
                      { raw->SetValue(self->m_asset->generateMipmaps); });
        }
        {
            auto editor = MakeRef<ui::toolkit::RangeEditor>(
                Allocator(), StringView(u8"Anisotropy"), m_asset->anisotropy, 1.0f, 16.0f,
                1.0f,
                Function<void(f32)>{[self](f32 v)
                                    {
                                        self->ApplyEdit(u8"anisotropy",
                                                        Function<void(pipeline::TextureAsset&)>{
                                                            [v](pipeline::TextureAsset& a)
                                                            { a.anisotropy = v; }});
                                    }},
                StringView(u8"Sampling"));
            AddEditor(editor.Get(),
                      [self, raw = editor.Get()]() { raw->SetValue(self->m_asset->anisotropy); });
        }
    }

    String TextureEditorPage::ResolvedFormatText() const
    {
        if (m_asset.Get() == nullptr)
        {
            return String(u8"-");
        }
        // The cook's own inputs, mirrored: sRGB flag, level-0 alpha scan, source size. An HDR
        // source resolves through the policy's HDR branch (uncompressed until BC6H).
        const bool srgb = m_asset->colorSpace == image::ImageColorSpace::Srgb;
        const u32 width = m_preview.Get() != nullptr ? m_preview->Width() : 0;
        const u32 height = m_preview.Get() != nullptr ? m_preview->Height() : 0;
        bool hasAlpha = false;
        if (m_preview.Get() != nullptr)
        {
            const Span<const u8> px = m_preview->PixelData();
            for (usize i = 0; i + 3 < px.Size(); i += 4)
            {
                if (px[i + 3] != 255)
                {
                    hasAlpha = true;
                    break;
                }
            }
        }
        const foundation::rhi::TextureFormat uncompressed =
            m_sourceFormat == image::PixelFormat::RGBA32F
                ? foundation::rhi::TextureFormat::RGBA32Float
                : foundation::rhi::TextureFormat::RGBA8Unorm;
        const bool multiChannel =
            m_preview.Get() != nullptr &&
            texcomp::HasDistinctChannels(m_preview->PixelData().Data(), width, height);
        const foundation::rhi::TextureFormat desktop = texcomp::ResolveCompressedFormat(
            m_asset->usage, srgb, hasAlpha, multiChannel, m_asset->compression, width, height,
            texcomp::DesktopProfile(), uncompressed);
        const foundation::rhi::TextureFormat mobile = texcomp::ResolveCompressedFormat(
            m_asset->usage, srgb, hasAlpha, multiChannel, m_asset->compression, width, height,
            texcomp::MobileProfile(), uncompressed);
        if (desktop == mobile)
        {
            return String(CookedFormatName(desktop));
        }
        return Format(u8"{}  |  mobile: {}", CookedFormatName(desktop), CookedFormatName(mobile));
    }

    void TextureEditorPage::BuildProfileRow(foundation::ui::FlexLayout& row)
    {
        // Profiles are ACTIONS (one click configures sampler + shape + usage + colorSpace
        // coherently, one undo entry) - buttons, not a stateful enum pretending to be a fact.
        TextureEditorPage* self = this;
        const auto profile = [&row, self](StringView label,
                                          void (pipeline::TextureAsset::*setup)())
        {
            auto button = MakeRef<foundation::ui::Button>(self->Allocator(), label);
            button->OnClick.Add(
                [self, setup](foundation::ui::ButtonBase*)
                {
                    self->ApplyEdit(u8"profile", Function<void(pipeline::TextureAsset&)>{
                                                     [setup](pipeline::TextureAsset& a)
                                                     { (a.*setup)(); }});
                });
            row.AddView(button.Get());
        };
        auto caption = MakeRef<foundation::ui::Label>(Allocator(),
                                                      StringView(u8"Apply profile:"));
        caption->FontSize.SetValue(12.0f);
        {
            auto lp = MakeRef<foundation::ui::FlexLayoutParams>(Allocator());
            lp->AlignSelf = foundation::ui::Align::Center;
            row.AddView(caption.Get(), lp);
        }
        profile(u8"UI", &pipeline::TextureAsset::SetupForUI);
        profile(u8"Sprite", &pipeline::TextureAsset::SetupForSprite);
        profile(u8"3D Surface", &pipeline::TextureAsset::SetupFor3D);
        profile(u8"Normal Map", &pipeline::TextureAsset::SetupForNormalMap);
        profile(u8"Data Mask", &pipeline::TextureAsset::SetupForDataMask);
        profile(u8"Equirect Sky", &pipeline::TextureAsset::SetupForEquirectangularSkybox);
        profile(u8"Cubemap Sky", &pipeline::TextureAsset::SetupForCubemapSkybox);
    }

    void TextureEditorPage::AddEditor(ui::toolkit::PropertyEditor* editor,
                                      Function<void()> refresher)
    {
        m_grid->AddProperty(RefPtr<ui::toolkit::PropertyEditor>(editor));
        ui::toolkit::PropertyEditor* raw = editor;
        m_refreshers.PushBack(Function<void()>{[raw, pull = Move(refresher)]()
                                               {
                                                   if (!raw->IsEditing())
                                                   {
                                                       pull();
                                                   }
                                               }});
    }

    Array<byte> TextureEditorPage::Snapshot() const
    {
        Array<byte> blob;
        if (m_asset.Get() == nullptr)
        {
            return blob;
        }
        MemoryStream stream;
        BinarySerializer ar(stream, SerializeMode::Write);
        // Versioned scope: snapshot blobs carry the asset's data-version chain exactly like
        // the envelope, so the read side accepts them (a scope-less blob would be refused).
        BeginVersionedPayload(ar, pipeline::TextureAsset::StaticType());
        m_asset->Serialize(ar);
        EndVersionedPayload(ar);
        const Span<const byte> bytes = stream.Bytes();
        blob.Reserve(bytes.Size());
        for (byte b : bytes)
        {
            blob.PushBack(b);
        }
        return blob;
    }

    void TextureEditorPage::ApplyBlob(const Array<byte>& blob)
    {
        if (m_asset.Get() == nullptr)
        {
            return;
        }
        MemoryStream stream;
        (void)stream.Write(blob.Data(), blob.Size());
        (void)stream.Seek(0, SeekOrigin::Begin);
        BinarySerializer ar(stream, SerializeMode::Read);
        BeginVersionedPayload(ar, pipeline::TextureAsset::StaticType());
        m_asset->Serialize(ar);
        EndVersionedPayload(ar);
        // Refresh in place (no grid rebuild - that would destroy editor views mid-event).
        for (const Function<void()>& refresher : m_refreshers)
        {
            refresher();
        }
        RefreshInfo();
    }

    void TextureEditorPage::ApplyEdit(StringView mergeKey,
                                      Function<void(pipeline::TextureAsset&)> mutate)
    {
        if (m_asset.Get() == nullptr)
        {
            return;
        }
        Array<byte> before = Snapshot();
        mutate(*m_asset);
        Array<byte> after = Snapshot();
        // The mutation already ran; Execute() re-applies `after` (idempotent).
        (void)Commands().Execute(UniquePtr<IEditorCommand>(
            Allocator().New<EditTextureCommand>(*this, mergeKey, Move(before), Move(after)),
            Allocator()));
    }

    Status TextureEditorPage::Save()
    {
        if (m_asset.Get() == nullptr || m_context->Project() == nullptr)
        {
            return Status{ErrorCode::NotFound};
        }
        foundation::content::Instance* instance =
            m_context->Project()->SourceDb().GetInstance(InstanceId());
        if (instance == nullptr)
        {
            return Status{ErrorCode::NotFound};
        }
        const Status saved = instance->WriteObject(*m_asset);
        if (saved.IsOk())
        {
            ClearDirty();
            // Refresh the cooked product so every bound proxy hot-swaps to the new settings.
            m_context->RequestCook(false);
            LOG_INFO(u8"Editor", u8"saved texture '{}'", m_title);
        }
        return saved;
    }

    const TypeInfo* TextureEditorPageFactory::PrimaryType() const
    {
        return &pipeline::TextureAsset::StaticType();
    }

    UniquePtr<EditorPage>
    TextureEditorPageFactory::CreatePage(EditorContext& context,
                                         foundation::content::Instance& instance)
    {
        auto* page = editor::EditorRootAllocator().New<TextureEditorPage>(context, instance);
        return UniquePtr<EditorPage>(page, editor::EditorRootAllocator());
    }
}
