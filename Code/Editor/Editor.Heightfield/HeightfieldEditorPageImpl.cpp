// SPDX-License-Identifier: MIT
// Copyright (c) 2026-Present Robert Campbell

// Editor::Heightfield - the `editor.heightfield` module (implementation).

module;
#include "Core/Prelude.h"
#include "Core/Log/Log.h"

module editor.heightfield;

import foundation.core;
import foundation.content;
import foundation.image;
import foundation.image.io;
import heightfield.pipeline;
import foundation.ui;
import foundation.ui.toolkit;
import editor.core;
import editor.app;

using namespace foundation::core;
namespace image = foundation::image;
namespace ui = foundation::ui;

namespace editor
{
    HeightfieldEditorPage::HeightfieldEditorPage(EditorContext& context,
                                                 foundation::content::Instance& instance)
        : m_context(&context), m_title(instance.Name())
    {
        SetInstanceId(instance.Id());

        RefPtr<ISerializable> object = instance.ReadObject();
        m_asset = RefPtr<pipeline::HeightfieldAsset>(Cast<pipeline::HeightfieldAsset>(object.Get()));
        if (m_asset.Get() == nullptr)
        {
            LOG_ERROR(u8"Editor", u8"heightfield '{}' failed to read - page opens empty", m_title);
        }
        else
        {
            LoadPreview();
        }
        m_undoBaseline = Snapshot();

        m_image = MakeRef<ui::ImageView>(DefaultAllocator());
        m_image->ScaleType.SetValue(ui::ScaleType::FitCenter);
        m_image->SetImage(m_preview.Get());

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

        RefreshInfo();
    }

    void HeightfieldEditorPage::LoadPreview()
    {
        m_preview.Reset();
        m_sourceWidth = 0;
        m_sourceHeight = 0;
        m_minSample = 0;
        m_maxSample = 0;
        if (m_asset.Get() == nullptr || m_asset->fileName.IsEmpty() ||
            m_context->Project() == nullptr)
        {
            return; // blank heightfield: no source image to preview
        }
        const String path =
            PathJoin(m_context->Project()->SourcesRoot().AsView(), m_asset->fileName.View());
        image::Image source;
        const Status loaded = image::io::LoadImage16(path.AsView(), source);
        if (!loaded.IsOk() || source.Width() == 0 || source.Height() == 0)
        {
            LOG_WARNING(u8"Editor", u8"heightfield source missing or undecodable: {}", path);
            return;
        }
        m_sourceWidth = source.Width();
        m_sourceHeight = source.Height();
        const usize count = static_cast<usize>(m_sourceWidth) * m_sourceHeight;
        const u16* samples = reinterpret_cast<const u16*>(source.PixelData().Data());

        m_minSample = 65535;
        m_maxSample = 0;
        Array<u8> rgba;
        rgba.Resize(count * 4);
        for (usize i = 0; i < count; ++i)
        {
            const u16 s = samples[i];
            m_minSample = s < m_minSample ? s : m_minSample;
            m_maxSample = s > m_maxSample ? s : m_maxSample;
            const u8 g = static_cast<u8>(s >> 8); // 16-bit height -> 8-bit grayscale
            rgba[i * 4 + 0] = g;
            rgba[i * 4 + 1] = g;
            rgba[i * 4 + 2] = g;
            rgba[i * 4 + 3] = 255;
        }
        m_preview = MakeUnique<image::OwnedImageData>(
            DefaultAllocator(), m_sourceWidth, m_sourceHeight, image::PixelFormat::RGBA8,
            Span<const u8>(rgba.Data(), rgba.Size()), image::ImageColorSpace::Linear);
    }

    void HeightfieldEditorPage::RefreshInfo()
    {
        if (m_asset.Get() == nullptr)
        {
            m_info->SetText(u8"Heightfield failed to load.");
            return;
        }
        const Float2 ws = m_asset->worldSize;
        if (m_preview.Get() == nullptr)
        {
            m_info->SetText(Format(u8"Blank heightfield (flat)  |  {} x {} grid  |  extent {} x {} m",
                                   m_asset->size, m_asset->size, ws.x, ws.y)
                                .AsView());
            return;
        }
        // Map the min/max samples to world Y through the asset's height range.
        const f32 range = m_asset->maxY - m_asset->minY;
        const f32 loY = m_asset->minY + range * (static_cast<f32>(m_minSample) / 65535.0f);
        const f32 hiY = m_asset->minY + range * (static_cast<f32>(m_maxSample) / 65535.0f);
        m_info->SetText(
            Format(u8"{}  |  source {} x {}  |  extent {} x {} m  |  height {} .. {} m",
                   m_asset->fileName.View(), m_sourceWidth, m_sourceHeight, ws.x, ws.y, loY, hiY)
                .AsView());
    }

    void HeightfieldEditorPage::BuildGrid()
    {
        m_grid->Clear();
        if (m_asset.Get() == nullptr)
        {
            return;
        }
        HeightfieldEditorPage* self = this;

        auto sizeRow = MakeRef<ui::toolkit::IntEditor>(
            DefaultAllocator(), u8"Grid Size (64k+1)", static_cast<i64>(m_asset->size), 65, 100000,
            Function<void(i64)>{[self](i64 v)
                                {
                                    self->m_asset->size = static_cast<i32>(v);
                                    self->CommitEdit(u8"size");
                                }},
            u8"Heightfield");
        m_grid->AddProperty(RefPtr<ui::toolkit::PropertyEditor>(sizeRow.Get()));

        auto worldRow = MakeRef<ui::toolkit::Float2Editor>(
            DefaultAllocator(), u8"World Size (XZ)", m_asset->worldSize, 1.0f, 100000.0f, 1.0f,
            Function<void(Float2)>{[self](Float2 v)
                                   {
                                       self->m_asset->worldSize = v;
                                       self->CommitEdit(u8"worldSize");
                                   }},
            u8"Heightfield");
        m_grid->AddProperty(RefPtr<ui::toolkit::PropertyEditor>(worldRow.Get()));

        auto minRow = MakeRef<ui::toolkit::FloatEditor>(
            DefaultAllocator(), u8"Min Height", static_cast<f64>(m_asset->minY), -100000.0, 100000.0,
            0.5, 2, Function<void(f64)>{[self](f64 v)
                                        {
                                            self->m_asset->minY = static_cast<f32>(v);
                                            self->CommitEdit(u8"minY");
                                        }},
            u8"Heightfield");
        m_grid->AddProperty(RefPtr<ui::toolkit::PropertyEditor>(minRow.Get()));

        auto maxRow = MakeRef<ui::toolkit::FloatEditor>(
            DefaultAllocator(), u8"Max Height", static_cast<f64>(m_asset->maxY), -100000.0, 100000.0,
            0.5, 2, Function<void(f64)>{[self](f64 v)
                                        {
                                            self->m_asset->maxY = static_cast<f32>(v);
                                            self->CommitEdit(u8"maxY");
                                        }},
            u8"Heightfield");
        m_grid->AddProperty(RefPtr<ui::toolkit::PropertyEditor>(maxRow.Get()));

        // Read-only source facts.
        auto stat = [&](StringView name, String value)
        {
            m_grid->AddProperty(RefPtr<ui::toolkit::PropertyEditor>(
                MakeRef<ui::toolkit::StringEditor>(DefaultAllocator(), name, value.AsView(),
                                                   Function<void(StringView)>{}, u8"Source")
                    .Get()));
        };
        stat(u8"File", m_asset->fileName.IsEmpty() ? String(u8"(blank)")
                                                   : String(m_asset->fileName.View()));
        if (m_preview.Get() != nullptr)
        {
            stat(u8"Source Size", Format(u8"{} x {}", m_sourceWidth, m_sourceHeight));
        }
    }

    void HeightfieldEditorPage::CommitEdit(StringView mergeKey)
    {
        Array<byte> after = Snapshot();
        (void)Commands().Execute(UniquePtr<IEditorCommand>(
            DefaultAllocator().New<EditCommand>(*this, mergeKey, m_undoBaseline, after),
            DefaultAllocator()));
        m_undoBaseline = Move(after);
        RefreshInfo();
        MarkDirty();
    }

    Array<byte> HeightfieldEditorPage::Snapshot() const
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

    void HeightfieldEditorPage::ApplyBlob(const Array<byte>& blob)
    {
        if (m_asset.Get() == nullptr || blob.IsEmpty())
        {
            return;
        }
        MemoryStream stream;
        (void)stream.Write(blob.Data(), blob.Size());
        (void)stream.Seek(0, SeekOrigin::Begin);
        BinarySerializer ar(stream, SerializeMode::Read);
        m_asset->Serialize(ar);
        m_undoBaseline = blob;
        BuildGrid(); // several editable rows: re-pull them all
        RefreshInfo();
        MarkDirty();
    }

    Status HeightfieldEditorPage::Save()
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
            m_context->RequestCook(false);
            LOG_INFO(u8"Editor", u8"saved heightfield '{}'", m_title);
        }
        return saved;
    }

    const TypeInfo* HeightfieldEditorPageFactory::PrimaryType() const
    {
        return &pipeline::HeightfieldAsset::StaticType();
    }

    UniquePtr<EditorPage>
    HeightfieldEditorPageFactory::CreatePage(EditorContext& context,
                                             foundation::content::Instance& instance)
    {
        auto* page = DefaultAllocator().New<HeightfieldEditorPage>(context, instance);
        return UniquePtr<EditorPage>(page, DefaultAllocator());
    }
}
