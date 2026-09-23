// SPDX-License-Identifier: MIT
// Copyright (c) 2026-Present Robert Campbell

// Editor::Terrain - the `editor.terrain` module.
//
// TerrainEditorPage (TerrainPage): the COMPOSITION + PREVIEW surface for a
// TerrainAsset. Left = a 3D orbit preview of the cooked terrain product (a TerrainComponent bound by
// the asset guid, on the shared PreviewViewport substrate - mesh/material precedent). Right = the
// authored fields: the heightfield + splatmap references (pickers), the layer list (albedo + normal +
// ORM + height + mask refs + tile scale per layer), castShadows, the height-blend contrast, and stats. Editing rewrites the asset, pushes a merge-keyed undo
// command, and Save writes it back + re-cooks so the bound TerrainResource hot-swaps. Brushes NEVER
// live here: sculpt + splat paint are scene-viewport IViewportTools; this
// page keeps ONE input-routing path by staying a composition + preview surface.
//
// GCC module hygiene: the heavy engine.terrain / engine.render / engine.scene imports stay in the
// impl unit (interface stays lean - see PreviewViewport.cppm's note).

module;
#include "Core/Prelude.h"

export module editor.terrain;

export import :splatmap_thumbnail;

import foundation.core;
import foundation.content;
import foundation.graphics;
import foundation.runtime;
import foundation.runtime.client;
import foundation.resource;
import foundation.scene; // EntityHandle
import foundation.ui;
import foundation.ui.runtime;
import terrain.pipeline;          // pipeline::TerrainAsset (the authored fields)
import foundation.terrain.resource; // TerrainResource (the cooked product for the preview + stats)
import editor.core;
import editor.app;
import editor.preview; // PreviewViewport (shared viewport + preview scene + camera + render loop)

export import :sculpt; // the scene-viewport terrain sculpt brush (IViewportTool) + the provider
export import :splat;  // the scene-viewport terrain splat (layer-weight) brush
export import :hole;   // the scene-viewport terrain hole brush (Specs/terrain-holes.md)

using namespace foundation::core;

export namespace editor
{
    namespace runtime = foundation::runtime;
    namespace ui = foundation::ui;
    namespace resource = foundation::resource;
    namespace terrain = foundation::terrain;

    class TerrainEditorPage final : public app::UIEditorPage
    {
    public:
        TerrainEditorPage(EditorContext& context, runtime::IApplicationHost& host,
                          ui::runtime::UIHost& uiHost, foundation::content::Instance& instance);

        [[nodiscard]] foundation::ui::View* ContentView() override { return m_content.Get(); }
        [[nodiscard]] StringView Title() const override { return m_title.AsView(); }
        [[nodiscard]] Status Save() override;

        void OnUpdate(runtime::IApplicationHost&, f32 dt) override;
        void OnRenderWindow(runtime::IApplicationHost&,
                            foundation::graphics::FrameContext& frame) override;
        void OnClose() override;

    private:
        void BuildPreviewScene();       // one TerrainComponent entity + a seeded sun
        void BindTerrain();             // (re)bind the cooked product by asset guid + reframe + stats
        void PointComponentAtTerrain(terrain::TerrainResource* product);
        void FramePreview(terrain::TerrainResource* product);
        void RebuildFields();           // (re)build the right pane from the current asset
        void RebuildFieldsDeferred();   // safe from inside a UI event (defers via the mutation queue)

        // Open the asset picker for a reference; `apply` writes the picked guid onto the right field
        // (a callback, so layer-index safety survives array edits), then commit + rebind + rebuild.
        void PickReference(StringView assetTypeName, StringView mergeKey,
                           Function<void(const Guid&)> apply);
        void AddLayer();
        void RemoveLayer(u32 index);
        enum class PaletteMap { Normal, Orm, Height, Mask };         // which optional per-layer map
        void SetPaletteMap(PaletteMap map, u32 index, const Guid& g); // set per-layer normal/ORM/height/mask
        void RemapWeightsOnRemove(u32 removedIndex); // frees removed-layer slots; decrements above
        void CreateSplatmap(i32 size); // author + assign a new blank splatmap asset (composition)

        // Undo/save (heightfield-page pattern): snapshot the asset to bytes, push a merge-keyed command.
        [[nodiscard]] Array<byte> Snapshot() const;
        void ApplyBlob(const Array<byte>& blob);
        void CommitEdit(StringView mergeKey);

        class EditCommand final : public IEditorCommand
        {
        public:
            EditCommand(TerrainEditorPage& page, StringView mergeKey, Array<byte> before,
                        Array<byte> after)
                : m_page(&page), m_mergeKey(mergeKey), m_before(Move(before)), m_after(Move(after))
            {
            }
            [[nodiscard]] bool Execute() override
            {
                m_page->ApplyBlob(m_after);
                return true;
            }
            void Undo() override { m_page->ApplyBlob(m_before); }
            [[nodiscard]] StringView TypeId() const override { return u8"edit_terrain"; }
            [[nodiscard]] bool MergeInto(IEditorCommand& previous) override
            {
                auto& prev = static_cast<EditCommand&>(previous);
                if (prev.m_page != m_page || prev.m_mergeKey.AsView() != m_mergeKey.AsView())
                {
                    return false;
                }
                prev.m_after = Move(m_after);
                return true;
            }

        private:
            TerrainEditorPage* m_page;
            String m_mergeKey;
            Array<byte> m_before;
            Array<byte> m_after;
        };

        EditorContext* m_context = nullptr;
        runtime::IApplicationHost* m_host = nullptr;
        ui::runtime::UIHost* m_uiHost = nullptr;
        String m_title;

        RefPtr<pipeline::TerrainAsset> m_asset;
        UniquePtr<PreviewViewport> m_preview;
        foundation::scene::EntityHandle m_entity;
        resource::Proxy<terrain::TerrainResource> m_terrainProxy; // cooked product (follows reloads)
        const void* m_lastProduct = nullptr;                      // product identity - detects cook

        RefPtr<foundation::ui::FlexLayout> m_fields; // the right pane (rebuilt on structural edits)
        RefPtr<foundation::ui::View> m_content;
        Array<byte> m_undoBaseline;
    };

    class TerrainEditorPageFactory final : public IEditorPageFactory
    {
    public:
        TerrainEditorPageFactory(runtime::IApplicationHost& host, ui::runtime::UIHost& uiHost)
            : m_host(&host), m_uiHost(&uiHost)
        {
        }
        [[nodiscard]] const TypeInfo* PrimaryType() const override
        {
            return &pipeline::TerrainAsset::StaticType();
        }
        [[nodiscard]] UniquePtr<EditorPage>
        CreatePage(EditorContext& context, foundation::content::Instance& instance) override;

    private:
        runtime::IApplicationHost* m_host;
        ui::runtime::UIHost* m_uiHost;
    };

    // Human-readable stat lines for a cooked terrain (grid size / chunks / layers / cast shadows).
    // Free + pure so it is unit-tested without a live host (MeshStatLines precedent).
    [[nodiscard]] Array<String> TerrainStatLines(const terrain::TerrainResource& product);

    void RegisterTerrainEditor(EditorContext& context, runtime::IApplicationHost& host,
                               ui::runtime::UIHost& uiHost);

    /// Register the terrain brush TOOL PANELS (sculpt + splat) into the viewport-tool-panel registry
    /// (editor.app:tool_panel) - the on-screen brush settings the scene page's ViewportToolPanelHost
    /// mounts while a terrain tool is active. Call once at editor start (alongside the tool provider).
    void RegisterTerrainToolPanels();
}
