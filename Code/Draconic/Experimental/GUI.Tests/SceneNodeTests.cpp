// Draconic GUI - SceneNode coordinator tests: root lookup, deferred Close via the
// MutationQueue, and the update loop draining tree edits.
#include <doctest/doctest.h>
#include "Core/Prelude.h"
import draconic.core;
import draconic.gui;

using namespace draconic::gui;
namespace core = draconic::core;

namespace
{
    core::RefPtr<SceneNode> MakeScene()
    {
        return core::MakeRef<SceneNode>(core::DefaultAllocator());
    }
    core::RefPtr<Node> MakeNode() { return core::MakeRef<Node>(core::DefaultAllocator()); }
}

TEST_CASE("scene: nodes find the coordinator by walking to the root")
{
    auto root = MakeScene();
    auto mid = MakeNode();
    auto leaf = MakeNode();
    root->AddChild(mid.Get());
    mid->AddChild(leaf.Get());

    CHECK(leaf->GetRootNode() == root.Get());
    CHECK(leaf->GetActionManager() == root->GetActionManager());
    CHECK(leaf->GetMutationQueue() == root->GetMutationQueue());
}

TEST_CASE("scene: Close defers removal until update drains the queue")
{
    auto root = MakeScene();
    auto child = MakeNode();
    root->AddChild(child.Get());

    child->Close();
    CHECK(root->ChildCount() == 1); // not yet - deferred
    CHECK_FALSE(root->GetMutationQueue()->IsEmpty());

    root->Update(core::Duration::FromSeconds(0.016));
    CHECK(root->ChildCount() == 0); // drained
    CHECK(root->GetMutationQueue()->IsEmpty());
}

TEST_CASE("scene: Close without a coordinator removes immediately")
{
    auto parent = MakeNode(); // plain Node, not a SceneNode
    auto child = MakeNode();
    parent->AddChild(child.Get());

    child->Close();
    CHECK(parent->ChildCount() == 0); // immediate fallback
}

TEST_CASE("scene: update with nothing queued is harmless")
{
    auto root = MakeScene();
    root->Update(core::Duration::FromSeconds(0.016));
    CHECK(root->GetActionManager()->IsEmpty());
    CHECK(root->GetMutationQueue()->IsEmpty());
}

TEST_CASE("scene: OnUpdate hook fires each update")
{
    struct CountingScene : SceneNode
    {
        int ticks = 0;
        void OnUpdate(core::Duration) override { ++ticks; }
    };
    auto scene = core::MakeRef<CountingScene>(core::DefaultAllocator());
    scene->Update(core::Duration::FromSeconds(0.016));
    scene->Update(core::Duration::FromSeconds(0.016));
    CHECK(scene->ticks == 2);
}
