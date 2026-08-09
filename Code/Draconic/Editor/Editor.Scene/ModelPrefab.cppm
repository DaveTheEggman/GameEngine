// Editor::Scene - :model_prefab partition.
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

export module editor.scene:model_prefab;

import foundation.core;
import foundation.content;
import foundation.resource;
import foundation.materials;
import foundation.scene;
import foundation.scene.resource;
import engine.render;
import engine.animation;
import foundation.physics;
import engine.physics;
import modelimporter;
import editor.core;

using namespace foundation::core;

export namespace editor
{
    namespace scene = foundation::scene;
    namespace render = foundation::render;

    struct ModelPrefabResult
    {
        foundation::content::Instance* instance = nullptr;
        bool regenerated = false; // an existing prefab was refreshed (re-import)
    };

    /// Generate (or refresh) the hierarchy prefab for a model manifest instance. Pure
    /// content-DB work - the caller handles editor-side follow-ups (instance rebuild,
    /// open-page refresh, notices).
    [[nodiscard]] inline ModelPrefabResult
    GenerateModelPrefab(foundation::content::Instance& manifestInstance)
    {
        ModelPrefabResult result;
        RefPtr<ISerializable> object = manifestInstance.ReadObject();
        auto* asset = Cast<pipeline::ModelManifestAsset>(object.Get());
        if (asset == nullptr)
        {
            return result;
        }
        const foundation::model::ModelManifestSource& manifest = asset->manifest;

        // Author the hierarchy in a throwaway scene, then capture it as a prefab payload.
        scene::Scene scene(manifestInstance.Name());
        auto* meshes = scene.AddSystem<engine::render::MeshComponentManager>();
        auto* anims = scene.AddSystem<engine::animation::SkeletalAnimationComponentManager>();
        const bool hasCollision = !manifest.collisionGuids.IsEmpty();
        auto* rigidBodies = hasCollision
                                ? scene.AddSystem<engine::physics::RigidBodyComponentManager>()
                                : nullptr;
        auto* colliders =
            hasCollision ? scene.AddSystem<engine::physics::ColliderComponentManager>() : nullptr;

        scene::EntityHandle root = scene.CreateEntity(manifestInstance.Name());
        Array<scene::EntityHandle> entities;
        entities.Reserve(manifest.nodes.Size());
        for (const foundation::model::ModelNode& node : manifest.nodes)
        {
            scene::EntityHandle e = scene.CreateEntity(node.name.AsView());
            scene.SetLocalTransform(e, node.localTransform);
            entities.PushBack(e);
        }
        const bool animated = !manifest.skeletonGuid.IsNil() && !manifest.animationGuids.IsEmpty();
        for (usize i = 0; i < manifest.nodes.Size(); ++i)
        {
            const foundation::model::ModelNode& node = manifest.nodes[i];
            if (node.parentIndex >= 0 && static_cast<usize>(node.parentIndex) < entities.Size())
            {
                scene.SetParent(entities[i], entities[static_cast<usize>(node.parentIndex)]);
            }
            else
            {
                scene.SetParent(entities[i], root); // top-level node -> the prefab root
            }
            if (node.meshIndex < 0 ||
                static_cast<usize>(node.meshIndex) >= manifest.meshGuids.Size())
            {
                continue;
            }
            const usize meshIndex = static_cast<usize>(node.meshIndex);
            engine::render::MeshComponent& mc = meshes->Add(entities[i]);
            mc.mesh.SetId(manifest.meshGuids[meshIndex]);

            // The unified material list: submeshes index it by SubMesh::materialIndex, and
            // slot 0 covers single-material meshes and out-of-range indexes.
            for (const Guid& g : manifest.materialGuids)
            {
                foundation::resource::Ref<foundation::materials::Material> r;
                r.SetId(g);
                mc.materials.PushBack(r);
            }

            // Generated collision: each mesh node gets a cooked-shape collider that folds
            // into the STATIC RigidBody on the prefab root (hierarchy compounding).
            if (colliders != nullptr && meshIndex < manifest.collisionGuids.Size() &&
                !manifest.collisionGuids[meshIndex].IsNil())
            {
                engine::physics::ColliderComponent& cc = colliders->Add(entities[i]);
                cc.shape = foundation::physics::ShapeKind::Cooked;
                cc.collisionShape.SetId(manifest.collisionGuids[meshIndex]);
            }

            const bool skinned =
                (meshIndex < manifest.meshSkinned.Size()) && manifest.meshSkinned[meshIndex] != 0;
            if (skinned && animated)
            {
                // Feeds its OWN entity (meshEntities stays empty - it doesn't serialize yet).
                engine::animation::SkeletalAnimationComponent& ac = anims->Add(entities[i]);
                ac.skeleton.SetId(manifest.skeletonGuid);
                ac.clip.SetId(manifest.animationGuids[0]);
            }
        }

        if (rigidBodies != nullptr)
        {
            bool anyCollider = false;
            for (const Guid& g : manifest.collisionGuids)
            {
                anyCollider = anyCollider || !g.IsNil();
            }
            if (anyCollider)
            {
                engine::physics::RigidBodyComponent& body = rigidBodies->Add(root);
                body.motion = foundation::physics::MotionKind::Static;
                body.layer = foundation::physics::PhysicsLayer::Static;
                // The root body's OWN shape stays a degenerate box; the real geometry
                // comes from the descendant cooked colliders compounding in.
                body.halfExtents = Float3{0.01f, 0.01f, 0.01f};
            }
        }

        MemoryStream payload;
        if (!scene::CapturePrefab(scene, root, payload).IsOk())
        {
            return result;
        }

        // "Prefab" beside the manifest; an existing one is REUSED so its guid (and every
        // placed instance) survives re-import. A non-prefab squatting on the name loses to
        // a suffixed fallback rather than being clobbered.
        foundation::content::Group& group = manifestInstance.OwningGroup();
        foundation::content::Instance* prefab = group.GetInstance(u8"Prefab");
        if (prefab != nullptr && prefab->TypeName() != StringView(u8"PrefabDocument"))
        {
            prefab = group.GetInstance(u8"Prefab.2");
            if (prefab == nullptr)
            {
                prefab = group.CreateInstance(u8"Prefab.2", scene::PrefabDocument::StaticType());
            }
        }
        result.regenerated = prefab != nullptr;
        if (prefab == nullptr)
        {
            prefab = group.CreateInstance(u8"Prefab", scene::PrefabDocument::StaticType());
        }
        if (prefab == nullptr)
        {
            return result;
        }

        scene::PrefabDocument doc;
        doc.name = String(manifestInstance.Name());
        if (!prefab->WriteObject(doc).IsOk() ||
            !prefab->WriteData(u8"scene", payload.Bytes()).IsOk())
        {
            LOG_ERROR(u8"Editor", u8"model prefab write failed for '{}'",
                               manifestInstance.Name());
            result.regenerated = false;
            return result;
        }
        result.instance = prefab;
        return result;
    }
}
