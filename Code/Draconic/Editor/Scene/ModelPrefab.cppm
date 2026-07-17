// Draconic::EditorScene - :model_prefab partition.
//
// Model->prefab generation (prefabs P3): a model import's manifest (node hierarchy + cooked
// leaf guids) becomes a spawnable PrefabDocument named "Prefab" inside the model's group. The
// wiring mirrors the runtime model spawn (Sandbox): one entity per node, MeshComponents with
// mesh/material refs by guid (per-submesh refs for multi-material models), and a
// SkeletalAnimationComponent on each skinned mesh node (feeds its own entity) when the model
// brought a skeleton + clips.
//
// RE-IMPORT REGENERATES: the "Prefab" instance is found by name and reused (same guid), so
// placed instances rebuild through the standard prefab machinery. This lives in the scene
// editor plugin - not the importer - because it needs scene + component machinery the
// importer library (linked by the headless cooker) deliberately never links; the import flow
// reaches it through EditorContext's import listeners.

module;
#include "Core/Prelude.h"
#include "Core/Log/Log.h"

export module draconic.editor.scene:model_prefab;

import draconic.core;
import draconic.content;
import draconic.resource;
import draconic.materials;
import draconic.scene;
import draconic.scene.resource;
import draconic.render.subsystem;
import draconic.animation.subsystem;
import draconic.modelimporter;
import draconic.editor.core;

using namespace draconic::core;

export namespace draconic::editor
{
    namespace dscene = draconic::scene;
    namespace drender = draconic::render;
    namespace danim = draconic::animation;
    namespace mi = draconic::modelimporter;

    struct ModelPrefabResult
    {
        draconic::content::Instance* instance = nullptr;
        bool regenerated = false;   // an existing prefab was refreshed (re-import)
    };

    /// Generate (or refresh) the hierarchy prefab for a model manifest instance. Pure
    /// content-DB work - the caller handles editor-side follow-ups (instance rebuild,
    /// open-page refresh, notices).
    [[nodiscard]] inline ModelPrefabResult GenerateModelPrefab(draconic::content::Instance& manifestInstance)
    {
        ModelPrefabResult result;
        RefPtr<ISerializable> object = manifestInstance.ReadObject();
        auto* asset = Cast<mi::ModelManifestAsset>(object.Get());
        if (asset == nullptr) { return result; }
        const mi::ModelManifestSource& manifest = asset->manifest;

        // Author the hierarchy in a throwaway scene, then capture it as a prefab payload.
        dscene::Scene scene(manifestInstance.Name());
        auto* meshes = scene.AddSystem<drender::MeshComponentManager>();
        auto* anims = scene.AddSystem<danim::SkeletalAnimationComponentManager>();

        dscene::EntityHandle root = scene.CreateEntity(manifestInstance.Name());
        Array<dscene::EntityHandle> entities;
        entities.Reserve(manifest.nodes.Size());
        for (const mi::ModelNode& node : manifest.nodes)
        {
            dscene::EntityHandle e = scene.CreateEntity(node.name.AsView());
            scene.SetLocalTransform(e, node.localTransform);
            entities.PushBack(e);
        }
        const bool animated = !manifest.skeletonGuid.IsNil() && !manifest.animationGuids.IsEmpty();
        for (usize i = 0; i < manifest.nodes.Size(); ++i)
        {
            const mi::ModelNode& node = manifest.nodes[i];
            if (node.parentIndex >= 0 && static_cast<usize>(node.parentIndex) < entities.Size())
            {
                scene.SetParent(entities[i], entities[static_cast<usize>(node.parentIndex)]);
            }
            else
            {
                scene.SetParent(entities[i], root);   // top-level node -> the prefab root
            }
            if (node.meshIndex < 0
                || static_cast<usize>(node.meshIndex) >= manifest.meshGuids.Size())
            {
                continue;
            }
            const usize meshIndex = static_cast<usize>(node.meshIndex);
            drender::MeshComponent& mc = meshes->Add(entities[i]);
            mc.mesh.SetId(manifest.meshGuids[meshIndex]);

            // Whole-mesh material = the mesh's first part; per-submesh refs (indexed by
            // SubMesh::materialIndex = model material index) when the model is multi-material.
            const i32 matIdx = (meshIndex < manifest.meshMaterial.Size())
                ? manifest.meshMaterial[meshIndex] : -1;
            if (matIdx >= 0 && static_cast<usize>(matIdx) < manifest.materialGuids.Size())
            {
                mc.material.SetId(manifest.materialGuids[static_cast<usize>(matIdx)]);
            }
            if (manifest.materialGuids.Size() > 1)
            {
                for (const Guid& g : manifest.materialGuids)
                {
                    draconic::resource::Ref<draconic::materials::Material> r;
                    r.SetId(g);
                    mc.submeshMaterialRefs.PushBack(r);
                }
            }

            const bool skinned = (meshIndex < manifest.meshSkinned.Size())
                && manifest.meshSkinned[meshIndex] != 0;
            if (skinned && animated)
            {
                // Feeds its OWN entity (meshEntities stays empty - it doesn't serialize yet).
                danim::SkeletalAnimationComponent& ac = anims->Add(entities[i]);
                ac.skeleton.SetId(manifest.skeletonGuid);
                ac.clip.SetId(manifest.animationGuids[0]);
            }
        }

        MemoryStream payload;
        if (!dscene::CapturePrefab(scene, root, payload).IsOk()) { return result; }

        // "Prefab" beside the manifest; an existing one is REUSED so its guid (and every
        // placed instance) survives re-import. A non-prefab squatting on the name loses to
        // a suffixed fallback rather than being clobbered.
        draconic::content::Group& group = manifestInstance.OwningGroup();
        draconic::content::Instance* prefab = group.GetInstance(u8"Prefab");
        if (prefab != nullptr && prefab->TypeName() != StringView(u8"PrefabDocument"))
        {
            prefab = group.GetInstance(u8"Prefab.2");
            if (prefab == nullptr)
            {
                prefab = group.CreateInstance(u8"Prefab.2", dscene::PrefabDocument::StaticType());
            }
        }
        result.regenerated = prefab != nullptr;
        if (prefab == nullptr)
        {
            prefab = group.CreateInstance(u8"Prefab", dscene::PrefabDocument::StaticType());
        }
        if (prefab == nullptr) { return result; }

        dscene::PrefabDocument doc;
        doc.name = String(manifestInstance.Name());
        if (!prefab->WriteObject(doc).IsOk()
            || !prefab->WriteData(u8"scene", payload.Bytes()).IsOk())
        {
            DRACONIC_LOG_ERROR(u8"Editor", u8"model prefab write failed for '{}'",
                               manifestInstance.Name());
            result.regenerated = false;
            return result;
        }
        result.instance = prefab;
        return result;
    }
}
