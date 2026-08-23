// EditorContext + EditorPageRegistry + Selection tests: nearest-type factory dispatch
// (Traktor's type_difference contest), open/focus/close page lifecycle, undo routing to the
// active page, selection semantics (dedup, primary, toggle).

#include <doctest/doctest.h>

#include "Core/Prelude.h"
#include "Core/Reflection/Reflect.h"

import foundation.core;
import foundation.vfs;
import foundation.content;
import foundation.xml.serialization;
import editor.core;

using namespace foundation::core;
using namespace editor;

namespace
{
    class BaseAsset : public ISerializable
    {
        RTTI_OBJECT(BaseAsset, ISerializable)
    public:
        void Serialize(ISerializer& ar) override { (void)ar; }
    };

    class DerivedAsset : public BaseAsset
    {
        RTTI_OBJECT(DerivedAsset, BaseAsset)
    };

    class UnrelatedAsset : public ISerializable
    {
        RTTI_OBJECT(UnrelatedAsset, ISerializable)
    public:
        void Serialize(ISerializer& ar) override { (void)ar; }
    };

    class TestPage final : public EditorPage
    {
    public:
        explicit TestPage(StringView title) : m_title(title) {}
        [[nodiscard]] StringView Title() const override { return m_title.AsView(); }
        [[nodiscard]] Status Save() override
        {
            ClearDirty();
            return Status{};
        }

    private:
        String m_title;
    };

    class TestPageFactory final : public IEditorPageFactory
    {
    public:
        TestPageFactory(const TypeInfo& type, StringView title) : m_type(&type), m_title(title) {}
        [[nodiscard]] const TypeInfo* PrimaryType() const override { return m_type; }
        [[nodiscard]] UniquePtr<EditorPage> CreatePage(EditorContext&,
                                                       foundation::content::Instance&) override
        {
            return UniquePtr<EditorPage>(DefaultAllocator().New<TestPage>(m_title.AsView()),
                                         DefaultAllocator());
        }

    private:
        const TypeInfo* m_type;
        String m_title;
    };

    UniquePtr<IEditorPageFactory> MakeFactory(const TypeInfo& type, StringView title)
    {
        return UniquePtr<IEditorPageFactory>(DefaultAllocator().New<TestPageFactory>(type, title),
                                             DefaultAllocator());
    }

    void RegisterTestTypes()
    {
        GlobalTypeRegistry().Register(BaseAsset::StaticType());
        GlobalTypeRegistry().Register(DerivedAsset::StaticType());
        GlobalTypeRegistry().Register(UnrelatedAsset::StaticType());
        RegisterSerializable<BaseAsset>();
        RegisterSerializable<DerivedAsset>();
        RegisterSerializable<UnrelatedAsset>();
    }

    void RemoveDbTree(StringView root)
    {
        FileDelete(PathJoin(root, u8"a.xasset"));
        FileDelete(PathJoin(root, u8"b.xasset"));
        FileDelete(PathJoin(root, u8"c.xasset"));
        RemoveDirectory(root);
    }
}

RTTI_DEFINE_OBJECT(BaseAsset, "rtti::editor::editor::test")
RTTI_DEFINE_OBJECT(DerivedAsset, "rtti::editor::editor::test")
RTTI_DEFINE_OBJECT(UnrelatedAsset, "rtti::editor::editor::test")

TEST_CASE("editor-pages: registry nearest-type dispatch")
{
    EditorPageRegistry registry;
    registry.Register(MakeFactory(BaseAsset::StaticType(), u8"base"));

    // Base factory serves the derived type (base-chain walk)...
    IEditorPageFactory* found = registry.FindFactory(DerivedAsset::StaticType());
    REQUIRE(found != nullptr);
    CHECK(found->PrimaryType() == &BaseAsset::StaticType());

    // ...until a MORE SPECIFIC factory wins the distance contest.
    registry.Register(MakeFactory(DerivedAsset::StaticType(), u8"derived"));
    found = registry.FindFactory(DerivedAsset::StaticType());
    REQUIRE(found != nullptr);
    CHECK(found->PrimaryType() == &DerivedAsset::StaticType());

    // The base type still dispatches to the base factory.
    found = registry.FindFactory(BaseAsset::StaticType());
    REQUIRE(found != nullptr);
    CHECK(found->PrimaryType() == &BaseAsset::StaticType());

    // No factory covers an unrelated chain.
    CHECK(registry.FindFactory(UnrelatedAsset::StaticType()) == nullptr);
}

TEST_CASE("editor-context: open, focus, and close pages")
{
    RegisterTestTypes();

    const StringView dir = u8"scratch_editor_test_ctx_db";
    RemoveDbTree(dir);
    foundation::vfs::NativeFileSystem mount(dir);
    foundation::content::ContentDatabase db(mount, foundation::xml::XmlSerializerFactory(),
                                          u8".xasset");

    auto* a = db.RootGroup()->CreateInstance(u8"a", BaseAsset::StaticType());
    auto* b = db.RootGroup()->CreateInstance(u8"b", DerivedAsset::StaticType());
    auto* c = db.RootGroup()->CreateInstance(u8"c", UnrelatedAsset::StaticType());
    REQUIRE(a != nullptr);
    REQUIRE(b != nullptr);
    REQUIRE(c != nullptr);

    EditorContext ctx;
    i32 pagesChanged = 0;
    ctx.OnPagesChanged = [&pagesChanged]() { ++pagesChanged; };
    ctx.Pages().Register(MakeFactory(BaseAsset::StaticType(), u8"base"));

    // Open a page; it becomes active.
    EditorPage* pageA = ctx.OpenPage(*a);
    REQUIRE(pageA != nullptr);
    CHECK(ctx.ActivePage() == pageA);
    CHECK(ctx.OpenPages().Size() == 1);
    CHECK(pagesChanged == 1);

    // The derived instance dispatches through the base factory.
    EditorPage* pageB = ctx.OpenPage(*b);
    REQUIRE(pageB != nullptr);
    CHECK(ctx.ActivePage() == pageB);
    CHECK(ctx.OpenPages().Size() == 2);

    // Re-opening the same instance focuses the existing page instead of duplicating it.
    CHECK(ctx.OpenPage(*a) == pageA);
    CHECK(ctx.ActivePage() == pageA);
    CHECK(ctx.OpenPages().Size() == 2);

    // No factory for the unrelated type.
    CHECK(ctx.OpenPage(*c) == nullptr);

    // Closing the active page activates a surviving neighbor.
    ctx.ClosePage(pageA);
    CHECK(ctx.OpenPages().Size() == 1);
    CHECK(ctx.ActivePage() == pageB);
    ctx.ClosePage(pageB);
    CHECK(ctx.OpenPages().Size() == 0);
    CHECK(ctx.ActivePage() == nullptr);

    RemoveDbTree(dir);
}

TEST_CASE("editor-context: NotifyProjectSettingsChanged fires the subscribed hook")
{
    // The settings dialog fires this after a successful save; the app re-applies
    // settings-derived session state (default UI font/theme binds) - the fix for a
    // changed default font keeping its OLD bind until project reopen.
    EditorContext ctx;
    int fired = 0;
    ctx.NotifyProjectSettingsChanged(); // unsubscribed: safe no-op
    ctx.OnProjectSettingsChanged = [&fired]() { ++fired; };
    ctx.NotifyProjectSettingsChanged();
    ctx.NotifyProjectSettingsChanged();
    CHECK(fired == 2);
}

TEST_CASE("editor-context: IsCookBusy defaults to not-busy and reads the wired query")
{
    // PIE's cook gate (GameEditorPage::Play latches, OnUpdate polls this): unwired
    // (tests, no project) must read NOT busy so a deferred start never hangs.
    EditorContext ctx;
    CHECK_FALSE(ctx.IsCookBusy());
    bool busy = true;
    ctx.CookBusy = [&busy]() { return busy; };
    CHECK(ctx.IsCookBusy());
    busy = false;
    CHECK_FALSE(ctx.IsCookBusy());
    ctx.CookBusy = {};
    CHECK_FALSE(ctx.IsCookBusy()); // unwired again (project closed) - never busy
}

TEST_CASE("editor-context: open-asset interceptors claim newest-first and unregister cleanly")
{
    RegisterTestTypes();
    const StringView dir = u8"scratch_editor_test_ctx_intercept";
    RemoveDbTree(dir);
    foundation::vfs::NativeFileSystem mount(dir);
    foundation::content::ContentDatabase db(mount, foundation::xml::XmlSerializerFactory(),
                                          u8".xasset");
    auto* a = db.RootGroup()->CreateInstance(u8"a", BaseAsset::StaticType());
    REQUIRE(a != nullptr);

    EditorContext ctx;

    // No interceptors -> the open is not claimed.
    CHECK(!ctx.TryInterceptOpenAsset(*a));

    // The NEWEST registration is consulted first; a false answer falls through to older ones.
    Array<i32> order;
    const u64 first = ctx.AddOpenAssetInterceptor(
        [&order](foundation::content::Instance&)
        {
            order.PushBack(1);
            return true;
        });
    const u64 second = ctx.AddOpenAssetInterceptor(
        [&order](foundation::content::Instance&)
        {
            order.PushBack(2);
            return false;
        });
    CHECK(ctx.TryInterceptOpenAsset(*a));
    REQUIRE(order.Size() == 2);
    CHECK(order[0] == 2); // newest first
    CHECK(order[1] == 1); // fell through to the older claimer

    // A true answer short-circuits: older interceptors are never consulted.
    order.Clear();
    ctx.RemoveOpenAssetInterceptor(second);
    const u64 third = ctx.AddOpenAssetInterceptor(
        [&order](foundation::content::Instance&)
        {
            order.PushBack(3);
            return true;
        });
    CHECK(ctx.TryInterceptOpenAsset(*a));
    REQUIRE(order.Size() == 1);
    CHECK(order[0] == 3);

    // All removed -> back to unclaimed (removal by id, order-independent).
    ctx.RemoveOpenAssetInterceptor(first);
    ctx.RemoveOpenAssetInterceptor(third);
    CHECK(!ctx.TryInterceptOpenAsset(*a));

    RemoveDbTree(dir);
}

TEST_CASE("editor-context: adopted instance-less pages share the ownership flow")
{
    EditorContext context;

    // Adopt (the Game tab's path): owned by the context, becomes active, nil instance id.
    auto page = MakeUnique<TestPage>(DefaultAllocator(), u8"Game");
    EditorPage* raw = context.AdoptPage(UniquePtr<EditorPage>(page.Release(), DefaultAllocator()));
    REQUIRE(raw != nullptr);
    CHECK(context.OpenPages().Size() == 1);
    CHECK(context.ActivePage() == raw);
    CHECK(raw->InstanceId().IsNil());

    // Close destroys through the same path as instance pages.
    context.ClosePage(raw);
    CHECK(context.OpenPages().Size() == 0);
    CHECK(context.ActivePage() == nullptr);

    // Null adopt is a no-op.
    CHECK(context.AdoptPage(UniquePtr<EditorPage>{}) == nullptr);
    CHECK(context.OpenPages().Size() == 0);
}

TEST_CASE("editor-context: undo/redo routes to the active page")
{
    RegisterTestTypes();

    const StringView dir = u8"scratch_editor_test_ctx_undo_db";
    RemoveDbTree(dir);
    foundation::vfs::NativeFileSystem mount(dir);
    foundation::content::ContentDatabase db(mount, foundation::xml::XmlSerializerFactory(),
                                          u8".xasset");
    auto* a = db.RootGroup()->CreateInstance(u8"a", BaseAsset::StaticType());
    REQUIRE(a != nullptr);

    EditorContext ctx;
    CHECK(!ctx.CanUndo()); // no active page
    ctx.Undo();            // safe no-op

    ctx.Pages().Register(MakeFactory(BaseAsset::StaticType(), u8"base"));
    EditorPage* page = ctx.OpenPage(*a);
    REQUIRE(page != nullptr);

    // A trivial command through the page's own stack.
    class Flip final : public IEditorCommand
    {
    public:
        explicit Flip(bool& b) : m_b(&b) {}
        [[nodiscard]] bool Execute() override
        {
            *m_b = !*m_b;
            return true;
        }
        void Undo() override { *m_b = !*m_b; }
        [[nodiscard]] StringView TypeId() const override { return u8"flip"; }

    private:
        bool* m_b;
    };

    bool flag = false;
    CHECK(page->Commands().Execute(
        UniquePtr<IEditorCommand>(DefaultAllocator().New<Flip>(flag), DefaultAllocator())));
    CHECK(flag);
    CHECK(page->IsDirty()); // command execution marks the page dirty

    CHECK(ctx.CanUndo());
    ctx.Undo();
    CHECK(!flag);
    CHECK(ctx.CanRedo());
    ctx.Redo();
    CHECK(flag);

    CHECK(page->Save().IsOk());
    CHECK(!page->IsDirty());

    RemoveDbTree(dir);
}

TEST_CASE("editor-selection: set, dedup, primary, toggle")
{
    Selection<i32> sel;
    i32 changes = 0;
    sel.OnChanged = [&changes]() { ++changes; };

    CHECK(sel.IsEmpty());
    CHECK(sel.Primary() == nullptr);

    sel.Set(5);
    CHECK(sel.Size() == 1);
    CHECK(*sel.Primary() == 5);
    CHECK(changes == 1);

    const i32 items[] = {3, 7, 3, 9}; // duplicate 3 removed, 3 stays primary
    sel.Set(Span<const i32>{items, 4});
    CHECK(sel.Size() == 3);
    CHECK(*sel.Primary() == 3);

    sel.Toggle(7); // present -> removed
    CHECK(sel.Size() == 2);
    CHECK(!sel.Contains(7));
    sel.Toggle(7); // absent -> added (at the back; primary unchanged)
    CHECK(sel.Contains(7));
    CHECK(*sel.Primary() == 3);

    sel.Clear();
    CHECK(sel.IsEmpty());

    const i32 changesAfterClear = changes;
    sel.Clear(); // clearing an empty selection does not notify
    CHECK(changes == changesAfterClear);
}

TEST_CASE("editor-context: Notify routes to OnNotice, falls back to the status bar")
{
    EditorContext ctx;
    Array<String> statuses;
    ctx.OnStatus = [&statuses](StringView text) { statuses.PushBack(String(text)); };

    // Unwired OnNotice -> status fallback (pages may Notify unconditionally).
    ctx.Notify(NoticeKind::Info, u8"hello");
    REQUIRE(statuses.Size() == 1u);
    CHECK(statuses[0].AsView() == StringView(u8"hello"));

    NoticeKind gotKind = NoticeKind::Info;
    String gotMessage;
    ctx.OnNotice = [&gotKind, &gotMessage](NoticeKind kind, StringView message)
    {
        gotKind = kind;
        gotMessage = String(message);
    };
    ctx.Notify(NoticeKind::Error, u8"cook failed");
    CHECK(gotKind == NoticeKind::Error);
    CHECK(gotMessage.AsView() == StringView(u8"cook failed"));
    CHECK(statuses.Size() == 1u); // wired notice does NOT double-post status
}

TEST_CASE("editor-context: script execution point set/clear + version stamps")
{
    EditorContext context;
    CHECK(!context.ScriptExecution().active);
    const u64 v0 = context.ScriptExecutionVersion();

    context.SetScriptExecutionPoint(u8"game.script", 12);
    CHECK(context.ScriptExecution().active);
    CHECK(context.ScriptExecution().file.AsView() == StringView(u8"game.script"));
    CHECK(context.ScriptExecution().line == 12);
    CHECK(context.ScriptExecutionVersion() != v0);

    // Re-set (a step to another line) bumps again; clear bumps once and goes inactive.
    const u64 v1 = context.ScriptExecutionVersion();
    context.SetScriptExecutionPoint(u8"game.script", 13);
    CHECK(context.ScriptExecutionVersion() != v1);
    const u64 v2 = context.ScriptExecutionVersion();
    context.ClearScriptExecutionPoint();
    CHECK(!context.ScriptExecution().active);
    CHECK(context.ScriptExecutionVersion() != v2);
    // Clearing while already clear is version-quiet (pollers stay idle).
    const u64 v3 = context.ScriptExecutionVersion();
    context.ClearScriptExecutionPoint();
    CHECK(context.ScriptExecutionVersion() == v3);
}

TEST_CASE("editor-context: script value probe slot")
{
    EditorContext context;
    CHECK(!context.ScriptValueProbe); // absent by default

    context.ScriptValueProbe = [](StringView identifier) -> String
    {
        if (identifier == StringView(u8"speed"))
        {
            return String(u8"4.5 : float");
        }
        return String();
    };
    REQUIRE(context.ScriptValueProbe);
    CHECK(context.ScriptValueProbe(u8"speed").AsView() == StringView(u8"4.5 : float"));
    CHECK(context.ScriptValueProbe(u8"unknown").IsEmpty());

    context.ScriptValueProbe = {}; // a run's end clears it
    CHECK(!context.ScriptValueProbe);
}
