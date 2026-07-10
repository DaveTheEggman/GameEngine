// Smoke test for the toolkit NodeGraphCanvas: add nodes / ports, read them back, add & validate
// connections, remove-and-remap, selection toggles, and coordinate round-trips. No font/VG rendering,
// no input simulation (events fire only from mouse/key handlers).
#include <doctest/doctest.h>
#include "Core/Prelude.h"
import draconic.core;
import draconic.ui;
import draconic.ui.toolkit;

using namespace draconic::ui;
using namespace draconic::ui::toolkit;
using namespace draconic::core;
namespace core = draconic::core;

namespace
{
    // Builds a node with a single output port and a single input port (both untyped) plus a title.
    core::UniquePtr<NodeGraphNode> MakeNode(const char8_t* title)
    {
        auto node = core::MakeUnique<NodeGraphNode>(core::DefaultAllocator());
        node->Title = String(title);

        NodeGraphPort outPort;
        outPort.Direction = PortDirection::Output;
        outPort.Label = String(u8"Out");
        node->OutputPorts.PushBack(outPort);

        NodeGraphPort inPort;
        inPort.Direction = PortDirection::Input;
        inPort.Label = String(u8"In");
        node->InputPorts.PushBack(inPort);

        return node;
    }
}

TEST_CASE("toolkit-nodegraph: AddAndReadNodes")
{
    auto cv = core::MakeRef<NodeGraphCanvas>(core::DefaultAllocator());

    CHECK(cv->NodeCount() == 0);
    CHECK(cv->ConnectionCount() == 0);
    CHECK(cv->Zoom() == doctest::Approx(1.0f));

    const i32 a = cv->AddNode(MakeNode(u8"Node A"));
    const i32 b = cv->AddNode(MakeNode(u8"Node B"));
    const i32 c = cv->AddNode(MakeNode(u8"Node C"));
    CHECK(a == 0);
    CHECK(b == 1);
    CHECK(c == 2);
    CHECK(cv->NodeCount() == 3);

    CHECK(cv->GetNode(0)->Title == u8"Node A");
    CHECK(cv->GetNode(2)->Title == u8"Node C");
    // Out-of-range returns null.
    CHECK(cv->GetNode(5) == nullptr);
    CHECK(cv->GetNode(-1) == nullptr);
}

TEST_CASE("toolkit-nodegraph: ConnectionsValidateAndCount")
{
    auto cv = core::MakeRef<NodeGraphCanvas>(core::DefaultAllocator());
    cv->AddNode(MakeNode(u8"A"));
    cv->AddNode(MakeNode(u8"B"));

    // Valid: A.out(0) -> B.in(0). Untyped ports connect.
    NodeGraphConnection conn{};
    conn.SourceNodeIndex = 0;
    conn.SourcePortIndex = 0;
    conn.DestNodeIndex = 1;
    conn.DestPortIndex = 0;
    const i32 idx = cv->AddConnection(conn);
    CHECK(idx == 0);
    CHECK(cv->ConnectionCount() == 1);

    // Self-connection is rejected.
    NodeGraphConnection self{};
    self.SourceNodeIndex = 0;
    self.SourcePortIndex = 0;
    self.DestNodeIndex = 0;
    self.DestPortIndex = 0;
    CHECK(cv->AddConnection(self) == -1);

    // Out-of-range node index is rejected.
    NodeGraphConnection oob{};
    oob.SourceNodeIndex = 0;
    oob.SourcePortIndex = 0;
    oob.DestNodeIndex = 9;
    oob.DestPortIndex = 0;
    CHECK(cv->AddConnection(oob) == -1);

    // Duplicate connection is rejected.
    CHECK(cv->AddConnection(conn) == -1);
    CHECK(cv->ConnectionCount() == 1);

    // Round-trip the stored connection.
    const NodeGraphConnection got = cv->GetConnection(0);
    CHECK(got.SourceNodeIndex == 0);
    CHECK(got.DestNodeIndex == 1);
}

TEST_CASE("toolkit-nodegraph: RemoveNodeRemapsConnections")
{
    auto cv = core::MakeRef<NodeGraphCanvas>(core::DefaultAllocator());
    cv->AddNode(MakeNode(u8"A")); // 0
    cv->AddNode(MakeNode(u8"B")); // 1
    cv->AddNode(MakeNode(u8"C")); // 2

    // Connect B.out -> C.in.
    NodeGraphConnection conn{};
    conn.SourceNodeIndex = 1;
    conn.SourcePortIndex = 0;
    conn.DestNodeIndex = 2;
    conn.DestPortIndex = 0;
    CHECK(cv->AddConnection(conn) == 0);

    // Remove node 0: B and C shift down to 0 and 1; the connection remaps to 0 -> 1.
    cv->RemoveNode(0);
    CHECK(cv->NodeCount() == 2);
    CHECK(cv->GetNode(0)->Title == u8"B");
    CHECK(cv->GetNode(1)->Title == u8"C");
    CHECK(cv->ConnectionCount() == 1);
    const NodeGraphConnection got = cv->GetConnection(0);
    CHECK(got.SourceNodeIndex == 0);
    CHECK(got.DestNodeIndex == 1);

    // Removing an endpoint drops the connection.
    cv->RemoveNode(1);
    CHECK(cv->NodeCount() == 1);
    CHECK(cv->ConnectionCount() == 0);
}

TEST_CASE("toolkit-nodegraph: SelectionToggles")
{
    auto cv = core::MakeRef<NodeGraphCanvas>(core::DefaultAllocator());
    cv->AddNode(MakeNode(u8"A"));
    cv->AddNode(MakeNode(u8"B"));

    CHECK(cv->GetNode(0)->IsSelected == false);
    cv->SelectNode(1);
    CHECK(cv->GetNode(1)->IsSelected == true);
    CHECK(cv->GetNode(0)->IsSelected == false);

    // Non-additive select replaces the selection.
    cv->SelectNode(0);
    CHECK(cv->GetNode(0)->IsSelected == true);
    CHECK(cv->GetNode(1)->IsSelected == false);

    // Additive select keeps both.
    cv->SelectNode(1, true);
    CHECK(cv->GetNode(0)->IsSelected == true);
    CHECK(cv->GetNode(1)->IsSelected == true);

    Array<i32> selected;
    cv->GetSelectedNodes(selected);
    CHECK(selected.Size() == 2);

    cv->ClearSelection();
    CHECK(cv->GetNode(0)->IsSelected == false);
    CHECK(cv->GetNode(1)->IsSelected == false);
}

TEST_CASE("toolkit-nodegraph: CoordinateRoundTrip")
{
    auto cv = core::MakeRef<NodeGraphCanvas>(core::DefaultAllocator());
    CHECK(cv->Zoom() == doctest::Approx(1.0f));

    const Float2 pt{ 123.0f, -45.0f };
    const Float2 back = cv->ScreenToCanvas(cv->CanvasToScreen(pt));
    CHECK(back.x == doctest::Approx(pt.x));
    CHECK(back.y == doctest::Approx(pt.y));
}

TEST_CASE("toolkit-nodegraph: ClearEmptiesEverything")
{
    auto cv = core::MakeRef<NodeGraphCanvas>(core::DefaultAllocator());
    cv->AddNode(MakeNode(u8"A"));
    cv->AddNode(MakeNode(u8"B"));
    NodeGraphConnection conn{};
    conn.SourceNodeIndex = 0;
    conn.SourcePortIndex = 0;
    conn.DestNodeIndex = 1;
    conn.DestPortIndex = 0;
    cv->AddConnection(conn);

    cv->Clear();
    CHECK(cv->NodeCount() == 0);
    CHECK(cv->ConnectionCount() == 0);
}
